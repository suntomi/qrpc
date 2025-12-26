#include "qrpc.h"

#include <ares.h>

#include "base/logger.h"

#include "base/defs.h"
#include "base/serial.h"
#include "base/timespec.h"

#include "json.hpp"
using json = nlohmann::json;

#include "qrpc/base.h"
#include "qrpc/transport.h"

#if defined(QRPC_THREADSAFE)
#undef QRPC_THREADSAFE
#define QRPC_THREADSAFE
#endif

#if defined(QRPC_BOOTSTRAP)
#undef QRPC_BOOTSTRAP
#define QRPC_BOOTSTRAP
#endif

using namespace qrpc;



// --------------------------
//
// helper
//
// --------------------------
enum InvalidHandleReason {
  IHR_CREATE_FAIL = 1,
  IHR_SERIAL_NOT_MATCH = 2,
  IHR_NOT_FOUND = 3,
};
template <class H> 
H INVALID_HANDLE(InvalidHandleReason ihr) {
  H h;
  h.p = reinterpret_cast<void *>(ihr);
  base::Serial::Clear(h.s);
  return h;
}
template <class H>
const char *INVALID_REASON(const H &h) {
  switch (reinterpret_cast<uintptr_t>(h.p)) {
    case IHR_CREATE_FAIL:
      return "create fail";
    case IHR_SERIAL_NOT_MATCH:
      return "serial not match";
    case IHR_NOT_FOUND:
      return "not found";
    default:
      if (base::Serial::IsEmpty(h.s)) {
        return "deallocated handle";
      } else {
        return "outdated handle";
      }
  }
}
// static inline bool IsOutgoing(bool is_client, qrpc_sid_t stream_id) {
//   return is_client ? ((stream_id % 2) != 0) : ((stream_id % 2) == 0);
// }
static inline qrpc_transport_config_t DefaultTransportConfig(qrpc_transport_type_t p) {
  switch (p) {
  case QRPC_TRANSPORT_WEBTRANSPORT:
    return {
      .proto = p,
      .webtx = {
        //application protocols
        .alpn = nullptr,
        //enable early data in handshake
        .enable_earty_data = false,
        //udp payload size in bytes
        .udp_payload_size = 0,
        //initial max data size in bytes.
        .initial_max_data = 0, .initial_max_data_bidi_local = 0, .initial_max_data_bidi_remote = 0,
        //initial stream count
        .initial_max_stream_bidi = 0,
        //total handshake time limit / no input limit / shutdown wait. default 1000ms/5000ms/5sec
        .handshake_timeout = 0ULL, .idle_timeout = 0ULL,
        //length of source connection id in bytes
        .source_connection_id_length = 8
      }
    };
  case QRPC_TRANSPORT_WEBRTC:
    return {
      .proto = p,
      .webrtc = {
        .params = {
          // send buffer size of underlying session (TCP/UDP)
          .send_buffer_size = 256 * 1024,
          // timeout of underlying session
          .session_timeout = qrpc_time_sec(15),
          // webrtc's SCTP session timeout
          .connection_timeout = qrpc_time_sec(60),
          // http timeout, shutdown timeout
          .http_timeout = qrpc_time_sec(5), .shutdown_timeout = qrpc_time_sec(3),
          // consent check interval
          .consent_check_interval = qrpc_time_sec(10),
          // fingerprint algorithm of DTLS
          // any of "sha-1", "sha-224", "sha-256", "sha-384", "sha-512"
          .fingerprint_algorithm = "sha-256",
        },
        // rtp config
        .rtp = {
          .initial_outgoing_bitrate = 600000,
          .max_incoming_bitrate = 0,
          .max_outgoing_bitrate = 0,
          .min_outgoing_bitrate = 0,
        }
      }
    };
  default:
    logger::die({{"ev","invalid wire proto"},{"proto",p}});
  }
}

#define no_ret_closure_call_with_check(__pclsr, ...) \
  if ((__pclsr).proc != nullptr) { \
    (__pclsr).proc((__pclsr).arg, __VA_ARGS__); \
  }



// --------------------------
//
// misc API
//
// --------------------------
QRPC_THREADSAFE const char *qrpc_error_str(qrpc_error_t code, int /* detail_code */) {
  static const char *errstr[] = {
    "no error",
    "syscall fails",
    "timeout",
    "allocation fails",
    "unsupported",
    "target gone",
    "dependent library error",
    "user raise error",
    "resolve fails",
    "invalid value",
    "not enough size",
    "callback returns error"
  };
  if (code > 0 || -code >= bulkof(errstr)) {
    return "invalid code";
  }
  return errstr[-code];
}


// --------------------------
//
// client API
//
// --------------------------
QRPC_THREADSAFE qrpc_clconf_t qrpc_client_conf() {
  qrpc_clconf_t conf = {
    //dns config
    .dns = {
      .query_timeout = qrpc_time_sec(5),
      .poll_interval = qrpc_time_sec(10),
      .dns_hosts = nullptr,
      .n_dns_hosts = 0,
      .use_dns = true, .use_hosts = true,
      .use_round_robin = false,
    },
    .max_nfd = 1024,
    .poll_timeout_ns = 1000000
  };
  return conf;
}
QRPC_THREADSAFE qrpc_connect_conf_t qrpc_connect_conf(qrpc_client_t cl, const char *host, int port) {
  auto c = qrpc::Client::FromHandle(cl);
  qrpc_connect_conf_t conf = {
    .ep = {
      .host = host,
      .port = port,
    },
    .transport = DefaultTransportConfig(c->transport_type())
  };
  switch (c->transport_type()) {
    case QRPC_TRANSPORT_WEBRTC:
      conf.ep.webrtc = {
        .ip = nullptr, // auto detected
        .path = "qrpc",
        .in6 = false,
        .proto = QRPC_EPPROTOCOL_ALL
      };
      break;
    case QRPC_TRANSPORT_WEBTRANSPORT:
      break;
  }
  qrpc_closure_init_noop(conf.on_open, qrpc_on_client_conn_open_t);
  qrpc_closure_init_noop(conf.on_close, qrpc_on_client_conn_close_t);
  qrpc_closure_init_noop(conf.on_finalize, qrpc_on_client_conn_finalize_t);
  // default stream router  
  qrpc_closure_init_noop(conf.stream_router, qrpc_stream_router_t);
  return conf;
}
QRPC_THREADSAFE qrpc_client_t qrpc_client_create(const qrpc_clconf_t *conf) {
  auto l = qrpc::Client::New(*conf);
  return l->ToHandle();
}
QRPC_THREADSAFE void qrpc_client_connect(qrpc_client_t cl, const qrpc_connect_conf_t *conf) {
  auto c = qrpc::Client::FromHandle(cl);
  if (c->GetPartitionId() != base::Loop::g_partition_id()) {
    c->Enqueue([c, conf]() {
      c->Connect(*conf);
    });
  }
  //we are not smart aleck and wanna use ipv4 if possible
  c->Connect(*conf);
}
QRPC_THREADSAFE void qrpc_client_resolve(qrpc_client_t cl, int family_pref, const char *hostname, qrpc_on_resolve_host_t cb) {
  auto c = qrpc::Client::FromHandle(cl);
  if (c->GetPartitionId() != base::Loop::g_partition_id()) {
    std::string host = hostname;
    c->Enqueue([c, family_pref, host, cb]() {
      c->Resolve(family_pref, host, cb);
    });
  }
  c->Resolve(family_pref, hostname, cb);
}
QRPC_BOOTSTRAP void qrpc_client_destroy(qrpc_client_t cl) {
  auto c = qrpc::Client::FromHandle(cl);
  c->Close();
  delete c;
}
QRPC_BOOTSTRAP void qrpc_client_poll(qrpc_client_t cl) {
  auto c = qrpc::Client::FromHandle(cl);
  if (UNLIKELY(c->GetPartitionId() != base::Loop::g_partition_id())) {
    logger::die({{"ev","qrpc_client_poll called from non-owner thread"},});
  }
  qrpc::Client::FromHandle(cl)->Poll();
}
QRPC_THREADSAFE const char *qrpc_ntop(const char *src, qrpc_size_t srclen, char *dst, qrpc_size_t dstlen) {
  if (AsyncResolver::NtoP(src, srclen, dst, dstlen) < 0) {
    return nullptr;
  } else {
    return dst;
  }
}




// --------------------------
//
// server API
//
// --------------------------
QRPC_THREADSAFE qrpc_svconf_t qrpc_server_conf() {
  return qrpc_svconf_t{
    .n_worker = static_cast<int>(base::Syscall::GetCpuCores()), // TODO: get number of cpu cores
    .max_nfd = static_cast<int>(base::Syscall::GetFdLimit()), 
    .process_index = 0,
  };
}
QRPC_THREADSAFE qrpc_listen_conf_t qrpc_listen_conf(qrpc_server_t sv) {
  auto s = qrpc::Server::FromHandle(sv);
  qrpc_listen_conf_t conf = {
    //how meny sessions accepted per loop. default 1024
    .accept_per_loop = 0,
    //allocation hint about max session
    .max_session_hint = 0,
    //transport config see DefaultTransportConfig for default configss
    .transport = DefaultTransportConfig(s->transport_type()),
  };
  qrpc_closure_init_noop(conf.on_open, qrpc_on_server_conn_open_t);
  qrpc_closure_init_noop(conf.on_close, qrpc_on_server_conn_close_t);
  return conf;
}
QRPC_THREADSAFE qrpc_server_t qrpc_server_create(const qrpc_svconf_t *conf) {
  auto c = conf == nullptr ? qrpc_server_conf() : *conf;
  if (c.n_worker < 0 || c.n_worker > 0xFFFF) {
    logger::die({{"ev","invalid n_worker"},{"n_worker",c.n_worker}});
  }
  auto sv = new Server(c);
  return sv->ToHandle();
}
QRPC_BOOTSTRAP int qrpc_server_listen(qrpc_server_t sv, const qrpc_listen_conf_t *conf) {
  auto s = Server::FromHandle(sv);
  return s->Open(*conf);
}
QRPC_BOOTSTRAP void qrpc_server_start(qrpc_server_t sv, bool block) {
  auto s = Server::FromHandle(sv);
  s->Start(block);
}
QRPC_THREADSAFE void qrpc_server_join(qrpc_server_t sv) {
  auto s = Server::FromHandle(sv);
  s->Join();
  delete s;
}



// --------------------------
//
// conn API
//
// --------------------------
#define GET_FIRST(__x, ...) __x
#define OP_RAW(__proc, ...) do { \
  auto __h = GET_FIRST(__VA_ARGS__); \
  auto partition_id = base::Serial::GetPartitionId(__h.s); \
  if (partition_id != base::Loop::g_partition_id()) { \
    Worker::queue(partition_id).enqueue([__VA_ARGS__]() { \
      __proc; \
    }); \
  } else { \
    __proc; \
  } \
} while (0)
#define CONN_OP(__proc, ...) OP_RAW(do { \
  auto __c = base::Connection::FromHandle(GET_FIRST(__VA_ARGS__)); \
  if (__c != nullptr) { \
    __proc; \
  } \
} while(0), __VA_ARGS__)

QRPC_THREADSAFE void qrpc_conn_close(qrpc_conn_t conn) {
  CONN_OP(__c->Close(), conn);
}
QRPC_THREADSAFE void qrpc_conn_reset(qrpc_conn_t conn) {
  CONN_OP(__c->Reset(), conn);
} 
QRPC_THREADSAFE void qrpc_conn_validate(qrpc_conn_t conn, qrpc_on_conn_validate_t cb) {
  OP_RAW(qrpc_closure_call(cb, conn, qrpc_conn_is_valid(conn));, conn, cb);
}
QRPC_THREADSAFE void qrpc_conn_emit(qrpc_conn_t conn, qrpc_on_event_t cb) {
  OP_RAW(qrpc_closure_call(cb, conn);, conn, cb);
}
QRPC_CLOSURECALL bool qrpc_conn_is_client(qrpc_conn_t conn) {
  auto c = base::Connection::FromHandle(conn);
  if (c != nullptr) {
    return c->is_client();
  }
  return false;
}
QRPC_CLOSURECALL bool qrpc_conn_is_valid(qrpc_conn_t conn) {
  return base::Connection::FromHandle(conn) != nullptr;
}
QRPC_CLOSURECALL void *qrpc_conn_ctx(qrpc_conn_t conn) {
  auto c = base::Connection::FromHandle(conn);
  if (c != nullptr) {
    return c->context();
  }
  return nullptr;
}



// --------------------------
//
// stream API
//
// --------------------------
#define STREAM_OP(__proc, ...) OP_RAW(do { \
  auto __s = base::Stream::FromHandle(GET_FIRST(__VA_ARGS__)); \
  if (__s != nullptr) { \
    __proc; \
  } \
} while(0), __VA_ARGS__)
QRPC_THREADSAFE qrpc_stream_config_t qrpc_stream_conf(const char *name) {
  qrpc_stream_config_t conf = {
    .name = name,
    .ordered = true,
    .stream_id = 0,
    .max_packet_lifetime = 0,
    .max_retransmits = 0,
  };
  return conf;
}
QRPC_THREADSAFE void qrpc_conn_stream(qrpc_conn_t conn, const qrpc_stream_config_t *conf, void *ctx) {
  auto cf = *conf;
  CONN_OP(__c->OpenStream(base::Stream::Config::From(cf, ctx));, conn, cf, ctx);
}
QRPC_CLOSURECALL qrpc_conn_t qrpc_stream_conn(qrpc_stream_t s) {
  return base::Stream::FromHandle(s)->connection().ToHandle();
}
QRPC_CLOSURECALL qrpc_alarm_t qrpc_stream_alarm(qrpc_stream_t s) {
  return base::Stream::FromHandle(s)->connection().alarm_processor().ToHandle();
}
QRPC_CLOSURECALL bool qrpc_stream_is_valid(qrpc_stream_t s) {
  return base::Stream::FromHandle(s) != nullptr;
}
QRPC_THREADSAFE void qrpc_stream_validate(qrpc_stream_t s, qrpc_on_stream_validate_t cb) {
  OP_RAW(qrpc_closure_call(cb, s, qrpc_stream_is_valid(s)), s, cb);
}
QRPC_THREADSAFE void qrpc_stream_close(qrpc_stream_t s) {
  STREAM_OP(__s->Close(base::Stream::CloseReason {
    .code = QRPC_CLOSE_REASON_LOCAL,
    .detail_code = 0,
    .msg = "closed by user",
  }), s);
}
QRPC_THREADSAFE void qrpc_stream_send(qrpc_stream_t s, const void *data, qrpc_size_t datalen) {
  auto partition_id = base::Serial::GetPartitionId(s.s);
  if (partition_id != base::Loop::g_partition_id()) {
    auto p = base::Syscall::Memdup(data, datalen);
    Worker::queue(partition_id).enqueue([s, p, datalen]() {
      auto st = base::Stream::FromHandle(s);
      if (st != nullptr) {
        st->Send(static_cast<const char *>(p), datalen);
      }
      base::Syscall::MemFree(p);
    });
  } else {
      auto st = base::Stream::FromHandle(s);
      if (st != nullptr) {
        st->Send(static_cast<const char *>(data), datalen);
      }
  }
}
QRPC_THREADSAFE void qrpc_stream_task(qrpc_stream_t s, qrpc_on_stream_task_t cb) {
  STREAM_OP(qrpc_closure_call(cb, s), s, cb);
}
QRPC_CLOSURECALL void *qrpc_stream_ctx(qrpc_stream_t s) {
  return base::Stream::FromHandle(s)->context_ptr();
}
QRPC_CLOSURECALL qrpc_sid_t qrpc_stream_sid(qrpc_stream_t s) {
  return base::Stream::FromHandle(s)->id();
}



// // --------------------------
// //
// // rpc API
// //
// // --------------------------
// static inline void rpc_reply_common(qrpc_rpc_t rpc, qrpc_error_t result, qrpc_msgid_t msgid, const void *data, qrpc_size_t datalen) {
//   ASSERT(result <= 0);
//   Stream *st; Boxer *b;
//   UNWRAP_STREAM_OR_ENQUEUE(rpc, st, b, {
//     st->Handler<NqSimpleRPCStreamHandler>()->Reply(result, msgid, data, datalen);
//   }, {
//     b->InvokeStream(rpc.s, st, Boxer::OpCode::Reply, result, msgid, data, datalen);
//   }, result < 0 ? "nq_rpc_error" : "nq_rpc_reply");
// }


// QRPC_CLOSURECALL void qrpc_conn_rpc(qrpc_conn_t conn, const char *name, void *ctx) {
//   conn_stream_common(conn, name, ctx, "nq_conn_rpc");
// }
// QRPC_THREADSAFE qrpc_conn_t qrpc_rpc_conn(qrpc_rpc_t rpc) {
//   Stream *st;
//   UNWRAP_STREAM(rpc, st, ({
//     return Unwrapper::Stream2Conn(rpc.s, st);
//   }), "nq_rpc_conn");
//   return INVALID_HANDLE<qrpc_conn_t>(IHR_CONN_NOT_FOUND);
// }
// QRPC_CLOSURECALL qrpc_alarm_t qrpc_rpc_alarm(qrpc_rpc_t rpc) {
//   // TODO(iyatomi): if possible, make this real thread safe
//   return Unwrapper::UnwrapBoxer(rpc)->NewAlarm()->ToHandle();
// }
// QRPC_THREADSAFE bool qrpc_rpc_is_valid(qrpc_rpc_t rpc, qrpc_on_rpc_validate_t cb) {
//   Stream *st;
//   UNWRAP_STREAM(rpc, st, {
//     no_ret_closure_call_with_check(cb, rpc, nullptr);
//     return true;
//   }, "nq_rpc_is_valid");
//   no_ret_closure_call_with_check(cb, rpc, INVALID_REASON(rpc));
//   return false;
// }
// QRPC_THREADSAFE bool qrpc_rpc_outgoing(qrpc_rpc_t rpc, bool *p_valid) {
//   Stream *st;
//   UNWRAP_STREAM(rpc, st, {
//     *p_valid = true;
//     return IsOutgoing(Serial::IsClient(rpc.s), st->id());
//   }, "nq_stream_close");
//   *p_valid = false;
//   return false;
// }
// QRPC_THREADSAFE void qrpc_rpc_close(qrpc_rpc_t rpc) {
//   Unwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, ToStream(rpc), Boxer::OpCode::Disconnect);
// }
// QRPC_THREADSAFE void qrpc_rpc_call(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen, qrpc_on_rpc_reply_t on_reply) {
//   ASSERT(type > 0);
//   Stream *st; Boxer *b;
//   UNWRAP_STREAM_OR_ENQUEUE(rpc, st, b, {
//     st->Handler<NqSimpleRPCStreamHandler>()->Call(type, data, datalen, on_reply);
//   }, {
//     b->InvokeStream(rpc.s, st, Boxer::OpCode::Call, type, data, datalen, on_reply);
//   }, "nq_rpc_call");
// }
// QRPC_THREADSAFE void qrpc_rpc_call_ex(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen, qrpc_rpc_opt_t *opts) {
//   ASSERT(type > 0);
//   Stream *st; Boxer *b;
//   UNWRAP_STREAM_OR_ENQUEUE(rpc, st, b, {
//     st->Handler<NqSimpleRPCStreamHandler>()->CallEx(type, data, datalen, *opts);
//   }, {
//     b->InvokeStream(rpc.s, st, Boxer::OpCode::CallEx, type, data, datalen, *opts);
//   }, "nq_rpc_call_ex");
// }
// QRPC_THREADSAFE void qrpc_rpc_notify(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen) {
//   ASSERT(type > 0);
//   Stream *st; Boxer *b;
//   UNWRAP_STREAM_OR_ENQUEUE(rpc, st, b, {
//     st->Handler<NqSimpleRPCStreamHandler>()->Notify(type, data, datalen);
//   }, {
//     b->InvokeStream(rpc.s, st, Boxer::OpCode::Notify, type, data, datalen);
//   }, "nq_rpc_notify");
// }
// QRPC_THREADSAFE void qrpc_rpc_reply(qrpc_rpc_t rpc, qrpc_msgid_t msgid, const void *data, qrpc_size_t datalen) {
//   rpc_reply_common(rpc, QRPC_OK, msgid, data, datalen);
// }
// QRPC_THREADSAFE void qrpc_rpc_error(qrpc_rpc_t rpc, qrpc_msgid_t msgid, const void *data, qrpc_size_t datalen) {
//   rpc_reply_common(rpc, QRPC_EUSER, msgid, data, datalen);
// }
// QRPC_THREADSAFE void qrpc_rpc_task(qrpc_rpc_t rpc, qrpc_on_rpc_task_t cb) {
//   Unwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, ToStream(rpc), Boxer::OpCode::Task, qrpc_to_dyn_closure(cb));
// }
// QRPC_CLOSURECALL void *nq_rpc_ctx(qrpc_rpc_t rpc) {
//   Stream *st;
//   UNSAFE_UNWRAP_STREAM(rpc, st, {
//     return st->Context();
//   }, "nq_rpc_ctx");
// }
// QRPC_THREADSAFE qrpc_sid_t qrpc_rpc_sid(qrpc_rpc_t rpc) {
//   Stream *st;
//   UNWRAP_STREAM(rpc, st, {
//     return st->id();
//   }, "nq_rpc_sid");
//   return 0;
// }



// --------------------------
//
// time API
//
// --------------------------
QRPC_THREADSAFE qrpc_time_t qrpc_time_now() {
  return base::clock::now();
}
QRPC_THREADSAFE qrpc_unix_time_t qrpc_time_unix() {
  long s, us;
  base::clock::now(s, us);
  return s;
}
QRPC_THREADSAFE qrpc_time_t qrpc_time_sleep(qrpc_time_t d) {
  return base::clock::sleep(d);
}
QRPC_THREADSAFE qrpc_time_t qrpc_time_pause(qrpc_time_t d) {
  return base::clock::pause(d);
}
QRPC_THREADSAFE uint32_t *qrpc_time_to_spec(qrpc_time_t n) {
  static thread_local uint32_t spec[2];
  spec[0] = n / 1000 / 1000 / 1000;
  spec[1] = n % (1000 * 1000 * 1000);
  return spec;
}



// --------------------------
//
// alarm API
//
// --------------------------
QRPC_CLOSURECALL qrpc_alarm_id_t qrpc_alarm_set(qrpc_alarm_t a, qrpc_time_t invocation_ts, qrpc_on_alarm_t cb) {
  auto ap = base::AlarmProcessor::FromHandle(a);
  return ap->Set([cb, a]() {
    return qrpc_closure_call(cb, a);
  }, invocation_ts);
}
QRPC_CLOSURECALL void qrpc_alarm_cancel(qrpc_alarm_t a, qrpc_alarm_id_t id) {
  auto ap = base::AlarmProcessor::FromHandle(a);
  ap->Cancel(id);
}



// --------------------------
//
// log API
//
// --------------------------
QRPC_BOOTSTRAP void qrpc_log_config(const qrpc_logconf_t *conf) {
  if (conf == nullptr) {
    // TODO: provide default log config
    base::logger::die({{"ev", "no log config"}});
  } else if (conf->level >= static_cast<int>(base::logger::level::max) || conf->level < 0) {
    base::logger::die({{"ev", "invalid log level"}, {"level", conf->level}});
  }
  base::logger::configure(conf->callback, conf->id, conf->manual_flush, static_cast<base::logger::level>(conf->level));
}
QRPC_THREADSAFE void qrpc_log(qrpc_loglv_t lv, const char *msg, qrpc_logparam_t *params, int n_params) {
  json j = {
    {"ev", msg}
  };
  for (int i = 0; i < n_params; i++) {
    auto p = params[i];
    switch (p.type) {
    case QRPC_LOG_INTEGER:
      j[p.key] = p.value.n;
      break;
    case QRPC_LOG_STRING:
      j[p.key] = p.value.s;
      break;
    case QRPC_LOG_DECIMAL:
      j[p.key] = p.value.d;
      break;
    case QRPC_LOG_BOOLEAN:
      j[p.key] = p.value.b;
      break;
    }
  }
  base::logger::log((base::logger::level)(int)lv, j);
}
QRPC_THREADSAFE void qrpc_log_flush() {
  base::logger::flush();
}
