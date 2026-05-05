#include "qrpc.h"

#include "json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

struct ClientState {
  std::vector<std::string> texts{"aaaa", "bbbb", "cccc"};
  int closed{0};
  int test3_count{0};
  std::string error;
  qrpc_client_t client{nullptr};
  qrpc_conn_t conn{};
};

void SendJson(qrpc_stream_t stream, const json& payload) {
  auto body = payload.dump();
  qrpc_stream_send(stream, body.data(), body.size());
}

void OnStreamCloseNoop(void*, qrpc_stream_t) {}

bool OnTestOpen(void* arg, qrpc_stream_t stream, void**) {
  auto* state = static_cast<ClientState*>(arg);
  SendJson(stream, {
    {"hello", state->texts[state->closed]},
    {"ts", qrpc_time_now()},
    {"count", state->closed},
  });
  return true;
}

void OnTestRecord(void* arg, qrpc_stream_t stream, const void* data, qrpc_size_t datalen) {
  auto* state = static_cast<ClientState*>(arg);
  try {
    auto now = qrpc_time_now();
    auto payload = std::string(static_cast<const char*>(data), datalen);
    QLOG(INFO, "OnTestRecord", { QLOG_STR("payload", payload.c_str()) });
    auto resp = json::parse(payload);
    auto hello = resp.at("hello").get<std::string>();
    auto count = resp.at("count").get<uint64_t>();
    auto ts = resp.at("ts").get<qrpc_time_t>();
    if (count >= state->texts.size()) {
      state->error = "test count out of range";
      qrpc_conn_close(qrpc_stream_conn(stream));
      return;
    }
    if (hello != ("test:" + state->texts[count])) {
      state->error = "stream message hello wrong";
      qrpc_conn_close(qrpc_stream_conn(stream));
      return;
    }
    if (count < 2) {
      std::fprintf(stderr, "[qrpc-e2e-client] Data channel latency(%lld)\n", static_cast<long long>(now - ts));
      SendJson(stream, {
        {"hello", state->texts[count + 1]},
        {"count", count + 1},
        {"ts", now},
      });
    } else {
      qrpc_stream_close(stream);
    }
  } catch (const std::exception& e) {
    state->error = e.what();
    qrpc_conn_close(qrpc_stream_conn(stream));
  }
}

void OnTestClose(void* arg, qrpc_stream_t stream) {
  auto* state = static_cast<ClientState*>(arg);
  if (!state->error.empty()) {
    return;
  }
  auto conf = qrpc_stream_conf("test2");
  qrpc_conn_stream(qrpc_stream_conn(stream), &conf, nullptr);
}

bool OnTest2Open(void*, qrpc_stream_t stream, void**) {
  SendJson(stream, {{"streamName", "recv"}});
  return true;
}

void OnTest2Record(void* arg, qrpc_stream_t, const void*, qrpc_size_t) {
  auto* state = static_cast<ClientState*>(arg);
  QLOG(INFO, "OnTest2Record", { QLOG_STR("payload", "unexpected callback") });
  state->error = "test2.onread should not be called";
}

bool OnTest3Open(void* arg, qrpc_stream_t stream, void**) {
  auto* state = static_cast<ClientState*>(arg);
  state->test3_count = 0;
  SendJson(stream, {{"count", 0}});
  return true;
}

void OnTest3Record(void* arg, qrpc_stream_t stream, const void* data, qrpc_size_t datalen) {
  auto* state = static_cast<ClientState*>(arg);
  try {
    auto payload = std::string(static_cast<const char*>(data), datalen);
    QLOG(INFO, "OnTest3Record", { QLOG_STR("payload", payload.c_str()) });
    auto resp = json::parse(payload);
    auto count = resp.at("count").get<uint64_t>();
    state->test3_count = static_cast<int>(count + 1);
    SendJson(stream, {{"count", state->test3_count}});
  } catch (const std::exception& e) {
    state->error = e.what();
    qrpc_conn_close(qrpc_stream_conn(stream));
  }
}

void OnTest3Close(void* arg, qrpc_stream_t) {
  auto* state = static_cast<ClientState*>(arg);
  if (state->test3_count != 2 && state->error.empty()) {
    state->error = "test3.onclose count should be 2";
  }
}

bool OnRecvOpen(void* arg, qrpc_stream_t stream, void**) {
  auto* state = static_cast<ClientState*>(arg);
  SendJson(stream, {{"die", state->closed < 2}});
  return true;
}

void OnRecvRecord(void* arg, qrpc_stream_t stream, const void* data, qrpc_size_t datalen) {
  auto* state = static_cast<ClientState*>(arg);
  try {
    auto payload = std::string(static_cast<const char*>(data), datalen);
    QLOG(INFO, "OnRecvRecord", { QLOG_STR("payload", payload.c_str()) });
    auto resp = json::parse(payload);
    auto msg = resp.at("msg").get<std::string>();
    if (msg != "byebye") {
      state->error = "Data channel3 message msg wrong";
      qrpc_conn_close(qrpc_stream_conn(stream));
      return;
    }
    qrpc_conn_close(qrpc_stream_conn(stream));
  } catch (const std::exception& e) {
    state->error = e.what();
    qrpc_conn_close(qrpc_stream_conn(stream));
  }
}

qrpc_stream_handler_t MakeStreamHandler(
  ClientState* state,
  qrpc_on_stream_open_t_proc open_cb,
  qrpc_on_stream_record_t_proc record_cb,
  qrpc_on_stream_close_t_proc close_cb
) {
  qrpc_stream_handler_t handler{};
  qrpc_closure_init(handler.on_stream_open, open_cb, state);
  qrpc_closure_init(handler.on_stream_close, close_cb, state);
  qrpc_closure_init(handler.on_stream_record, record_cb, state);
  return handler;
}

qrpc_handler_entry_t* StreamRouter(void* arg, const char* label, qrpc_conn_t) {
  auto* state = static_cast<ClientState*>(arg);
  static qrpc_handler_entry_t test_handler{};
  static qrpc_handler_entry_t test2_handler{};
  static qrpc_handler_entry_t test3_handler{};
  static qrpc_handler_entry_t recv_handler{};
  static bool initialized = false;
  if (!initialized) {
    test_handler = {
      .type = STREAM,
      .stream = MakeStreamHandler(state, OnTestOpen, OnTestRecord, OnTestClose),
    };
    test2_handler = {
      .type = STREAM,
      .stream = MakeStreamHandler(state, OnTest2Open, OnTest2Record, OnStreamCloseNoop),
    };
    test3_handler = {
      .type = STREAM,
      .stream = MakeStreamHandler(state, OnTest3Open, OnTest3Record, OnTest3Close),
    };
    recv_handler = {
      .type = STREAM,
      .stream = MakeStreamHandler(state, OnRecvOpen, OnRecvRecord, OnStreamCloseNoop),
    };
    initialized = true;
  }
  if (std::strcmp(label, "test") == 0) {
    return &test_handler;
  }
  if (std::strcmp(label, "test2") == 0) {
    return &test2_handler;
  }
  if (std::strcmp(label, "test3") == 0) {
    return &test3_handler;
  }
  if (std::strcmp(label, "recv") == 0) {
    return &recv_handler;
  }
  return nullptr;
}

int OnConnOpen(void* arg, qrpc_conn_t conn, void** ppctx) {
  auto* state = static_cast<ClientState*>(arg);
  state->conn = conn;
  *ppctx = state;
  auto test_conf = qrpc_stream_conf("test");
  auto test3_conf = qrpc_stream_conf("test3");
  qrpc_conn_stream(conn, &test_conf, nullptr);
  qrpc_conn_stream(conn, &test3_conf, nullptr);
  return QRPC_OK;
}

qrpc_time_t OnConnClose(void* arg, qrpc_conn_t, const qrpc_close_reason_t* reason, bool) {
  auto* state = static_cast<ClientState*>(arg);
  if (reason->code == QRPC_CLOSE_REASON_PROTOCOL) {
    state->error = std::string("protocol error:") + std::string(reason->msg, reason->msglen);
    return qrpc_alarm_stop_rv();
  }
  if (state->closed < 2) {
    state->closed++;
    return qrpc_time_sec(2);
  }
  state->error = "success";
  return qrpc_alarm_stop_rv();
}

void OnConnFinalize(void*, qrpc_conn_t) {}

} // namespace

int main() {
  ClientState state;

  auto clconf = qrpc_client_conf();
  clconf.max_nfd = 1024;
  clconf.session_timeout = qrpc_time_sec(30);
  clconf.connect_timeout = qrpc_time_sec(10);
  auto client = qrpc_client_create(&clconf);
  state.client = client;

  auto conf = qrpc_connect_conf(client, "localhost", 8888);
  qrpc_closure_init(conf.stream_router, StreamRouter, &state);
  qrpc_closure_init(conf.on_open, OnConnOpen, &state);
  qrpc_closure_init(conf.on_close, OnConnClose, &state);
  qrpc_closure_init(conf.on_finalize, OnConnFinalize, &state);
  conf.transport.proto = QRPC_TRANSPORT_WEBRTC;
  conf.transport.webrtc.rtp = {
    .initial_outgoing_bitrate = 10000000,
    .max_outgoing_bitrate = 0,
    .max_incoming_bitrate = 10000000,
    .min_outgoing_bitrate = 0,
  };
  conf.transport.webrtc.params.max_outgoing_stream_size = 32;
  conf.transport.webrtc.params.initial_incoming_stream_size = 32;
  conf.transport.webrtc.params.send_buffer_size = 256 * 1024;
  conf.transport.webrtc.params.session_timeout = qrpc_time_sec(15);
  conf.transport.webrtc.params.connection_timeout = qrpc_time_sec(60);
  conf.transport.webrtc.params.shutdown_timeout = qrpc_time_sec(3);
  conf.transport.webrtc.params.http_timeout = qrpc_time_sec(5);
  conf.transport.webrtc.params.consent_check_interval = qrpc_time_sec(10);
  conf.transport.webrtc.params.fingerprint_algorithm = "sha-256";
  conf.ep.webrtc = {
    .ip = nullptr,
    .path = "/qrpc",
    .in6 = false,
    .proto = QRPC_WEBRTC_ENDPOINT_PROTOCOL_ALL,
  };

  qrpc_client_connect(client, &conf);
  while (state.error.empty()) {
    qrpc_client_poll(client);
  }
  qrpc_client_destroy(client);
  if (state.error == "success") {
    return 0;
  }
  std::fprintf(stderr, "[qrpc-e2e-client] %s\n", state.error.c_str());
  return 1;
}
