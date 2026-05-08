#include "qrpc.h"

#include "json.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace {

std::atomic<bool> g_alive{true};

void OnSignal(void*, qrpc_signal_event_t* ev) {
  if (ev->signum == SIGINT || ev->signum == SIGTERM) {
    QLOG(INFO, "OnSignal", { QLOG_INT("signum", ev->signum), QLOG_INT("reap_count", ev->reap_count) });
    g_alive = false;
  }
}

void SendJson(qrpc_stream_t stream, const json& payload) {
  auto body = payload.dump();
  qrpc_stream_send(stream, body.data(), body.size());
}

int OnConnOpen(void*, qrpc_conn_t, void**) {
  return QRPC_OK;
}

void OnConnClose(void*, qrpc_conn_t, const qrpc_close_reason_t* reason, bool remote) {
  const char* msg = reason->msg != nullptr ? reason->msg : "";
  QLOG(INFO, "OnConnClose", {
    QLOG_INT("code", reason->code),
    QLOG_BOOL("remote", remote),
    QLOG_INT("detail", reason->detail_code),
    QLOG_STR("msg", msg)
  });
}

bool OnStreamOpen(void*, qrpc_stream_t, void**) {
  return true;
}

void OnStreamClose(void*, qrpc_stream_t) {}

void OnTestRecord(void*, qrpc_stream_t stream, const void* data, qrpc_size_t datalen) {
  try {
    auto payload = std::string(static_cast<const char*>(data), datalen);
    QLOG(INFO, "OnTestRecord", { QLOG_STR("payload", payload.c_str()) });
    auto req = json::parse(payload);
    SendJson(stream, {
      {"hello", std::string("test:") + req.at("hello").get<std::string>()},
      {"ts", req.at("ts").get<uint64_t>()},
      {"count", req.at("count").get<uint64_t>()},
    });
  } catch (const std::exception& e) {
    QLOG(ERROR, "OnTestRecord parse error", { QLOG_STR("what", e.what()) });
    qrpc_stream_close(stream);
  }
}

void OnTest2Record(void*, qrpc_stream_t stream, const void* data, qrpc_size_t datalen) {
  try {
    auto payload = std::string(static_cast<const char*>(data), datalen);
    QLOG(INFO, "OnTest2Record", { QLOG_STR("payload", payload.c_str()) });
    auto req = json::parse(payload);
    auto conf = qrpc_stream_conf(req.at("streamName").get_ref<const std::string&>().c_str());
    qrpc_conn_stream(qrpc_stream_conn(stream), &conf, nullptr);
  } catch (const std::exception& e) {
    QLOG(ERROR, "OnTest2Record parse error", { QLOG_STR("what", e.what()) });
    qrpc_stream_close(stream);
  }
}

void OnTest3Record(void*, qrpc_stream_t stream, const void* data, qrpc_size_t datalen) {
  try {
    auto payload = std::string(static_cast<const char*>(data), datalen);
    QLOG(INFO, "OnTest3Record", { QLOG_STR("payload", payload.c_str()) });
    auto req = json::parse(payload);
    auto count = req.at("count").get<uint64_t>();
    if (count >= 2) {
      qrpc_stream_close(stream);
    } else {
      SendJson(stream, {{"count", count}});
    }
  } catch (const std::exception& e) {
    QLOG(ERROR, "OnTest3Record parse error", { QLOG_STR("what", e.what()) });
    qrpc_stream_close(stream);
  }
}

void OnRecvRecord(void*, qrpc_stream_t stream, const void* data, qrpc_size_t datalen) {
  try {
    auto payload = std::string(static_cast<const char*>(data), datalen);
    QLOG(INFO, "OnRecvRecord", { QLOG_STR("payload", payload.c_str()) });
    auto req = json::parse(payload);
    if (req.at("die").get<bool>()) {
      qrpc_conn_close(qrpc_stream_conn(stream));
    } else {
      SendJson(stream, {{"msg", "byebye"}});
    }
  } catch (const std::exception& e) {
    QLOG(ERROR, "OnRecvRecord parse error", { QLOG_STR("what", e.what()) });
    qrpc_conn_close(qrpc_stream_conn(stream));
  }
}

void OnIgnoreRecord(void*, qrpc_stream_t, const void*, qrpc_size_t) {}

bool OnMediaOpen(void*, qrpc_media_t, void**) {
  return true;
}

void OnMediaClose(void*, qrpc_media_t) {}

void OnMediaStateChange(void*, qrpc_media_t, const char*, const char*) {}

qrpc_media_handler_t* MediaRouter(void*, const char*, qrpc_conn_t) {
  static qrpc_media_handler_t handler{};
  static bool initialized = false;
  if (!initialized) {
    qrpc_closure_init(handler.on_media_open, OnMediaOpen, nullptr);
    qrpc_closure_init(handler.on_media_close, OnMediaClose, nullptr);
    qrpc_closure_init(handler.on_media_state_change, OnMediaStateChange, nullptr);
    initialized = true;
  }
  return &handler;
}

qrpc_stream_handler_t MakeStreamHandler(qrpc_on_stream_record_t_proc record_cb) {
  qrpc_stream_handler_t handler{};
  qrpc_closure_init(handler.on_stream_open, OnStreamOpen, nullptr);
  qrpc_closure_init(handler.on_stream_close, OnStreamClose, nullptr);
  qrpc_closure_init(handler.on_stream_record, record_cb, nullptr);
  return handler;
}

qrpc_handler_entry_t* StreamRouter(void*, const char* label, qrpc_conn_t) {
  static qrpc_handler_entry_t test_handler{
    .type = STREAM,
    .stream = MakeStreamHandler(OnTestRecord),
  };
  static qrpc_handler_entry_t test2_handler{
    .type = STREAM,
    .stream = MakeStreamHandler(OnTest2Record),
  };
  static qrpc_handler_entry_t test3_handler{
    .type = STREAM,
    .stream = MakeStreamHandler(OnTest3Record),
  };
  static qrpc_handler_entry_t recv_handler{
    .type = STREAM,
    .stream = MakeStreamHandler(OnRecvRecord),
  };
  static qrpc_handler_entry_t ignore_handler{
    .type = STREAM,
    .stream = MakeStreamHandler(OnIgnoreRecord),
  };

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
  return &ignore_handler;
}

} // namespace

int main() {
  if (qrpc_signal_init() < 0) {
    QLOG(ERROR, "failed to initialize signal handler", {});
    return 1;
  }
  qrpc_signal_handler_t signal_handler;
  qrpc_closure_init(signal_handler, OnSignal, nullptr);
  if (qrpc_signal_handle(SIGINT, signal_handler) < 0 ||
      qrpc_signal_handle(SIGTERM, signal_handler) < 0) {
    QLOG(ERROR, "failed to register signal handler", {});
    return 1;
  }

  qrpc_svconf_t svconf{
    .n_worker = 1,
    .max_nfd = 1024,
    .process_index = 0,
  };
  auto server = qrpc_server_create(&svconf);

  auto conf = qrpc_listen_conf(server);
  qrpc_closure_init(conf.on_open, OnConnOpen, nullptr);
  qrpc_closure_init(conf.on_close, OnConnClose, nullptr);
  qrpc_closure_init(conf.stream_router, StreamRouter, nullptr);
  qrpc_closure_init(conf.media_router, MediaRouter, nullptr);
  conf.transport.proto = QRPC_TRANSPORT_WEBRTC;
  conf.transport.webrtc.rtp = {
    .initial_outgoing_bitrate = 10000000,
    .max_outgoing_bitrate = 100000000,
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
  conf.port = 8888;
  conf.ep = {
    .host = nullptr,
    .port = 11111,
    .webrtc = {
      .ip = std::getenv("QRPC_E2E_SFU_IP"),
      .path = "/qrpc",
      .in6 = false,
      .proto = QRPC_WEBRTC_ENDPOINT_PROTOCOL_ALL,
    },
  };

  if (qrpc_server_listen(server, &conf) < 0) {
    QLOG(ERROR, "failed to listen", { QLOG_INT("port", conf.port) });
    qrpc_server_join(server);
    return 1;
  }

  qrpc_server_start(server, false);
  while (g_alive.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  qrpc_server_join(server);
  return 0;
}
