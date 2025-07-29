#include "qrpc.h"

#include <ares.h>

#include "base/logger.h"

#include "base/defs.h"
#include "base/timespec.h"

#include "json.hpp"
using json = nlohmann::json;

#include "qrpc/base.h"
#include "qrpc/serial.h"

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
// chaos modes
//
// --------------------------
#if defined(DEBUG)
static bool g_chaos_write = false;
extern bool chaos_write() {
  return g_chaos_write;
}
void chaos_init() {
  g_chaos_write = getenv("CHAOS") != nullptr;
}
#else
#define chaos_init()
#endif



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
  Serial::Clear(h.s);
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
      if (Serial::IsEmpty(h.s)) {
        return "deallocated handle";
      } else {
        return "outdated handle";
      }
  }
}
static inline bool IsOutgoing(bool is_client, qrpc_sid_t stream_id) {
  return is_client ? ((stream_id % 2) != 0) : ((stream_id % 2) == 0);
}
static inline qrpc_transport_config_t DefaultTransportConfig(qrpc_wire_proto_t p) {
  switch (p) {
  case QRPC_WIRE_PROTO_WEBTRANSPORT:
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
  case QRPC_WIRE_PROTO_WEBRTC:
    return {
      .proto = p,
      .webrtc = {
        // max outgoing stream of SCTP
        .max_outgoing_stream_size = 32,
        // initial incoming stream of SCTP
        .initial_incoming_stream_size = 32,
        // send buffer size of underlying session (TCP/UDP)
        .send_buffer_size = 256 * 1024,
        // timeout of underlying session
        .session_timeout = qrpc_time_sec(15),
        // webrtc's SCTP session timeout
        .connection_timeout = qrpc_time_sec(60),
        // fingerprint algorithm of DTLS
        // any of "sha-1", "sha-224", "sha-256", "sha-384", "sha-512"
        .fingerprint_algorithm = "sha-256",
        // WHIP signaling server path
        .whip_path = "qrpc",
      }
    };
  default:
    logger::die({{"ev","invalid wire proto"},{"proto",p}});
  }
}

static void lib_init() {
  //break some of the systems according to the env value "CHAOS"
  chaos_init();
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
  return errstr[-code];
}


// --------------------------
//
// client API
//
// --------------------------
QRPC_THREADSAFE qrpc_clconf_t qrpc_client_conf() {
  qrpc_clconf_t conf = {
    //transport config
    .transport = DefaultTransportConfig(QRPC_WIRE_PROTO_DEFAULT)
  };
  qrpc_closure_init_noop(conf.on_open, qrpc_on_client_conn_open_t);
  qrpc_closure_init_noop(conf.on_close, qrpc_on_client_conn_close_t);
  qrpc_closure_init_noop(conf.on_finalize, qrpc_on_client_conn_finalize_t);
  return conf;
}
// QRPC_THREADSAFE qrpc_client_t qrpc_client_create(int max_nfd, int max_stream_hint, const qrpc_dns_conf_t *dns_conf) {
//   lib_init(); //anchor
//   auto l = new ClientLoop(max_nfd, max_stream_hint);
//   if (l->Open(max_nfd, dns_conf) < 0) {
//     return nullptr;
//   }
//   return l->ToHandle();
// }
// QRPC_BOOTSTRAP void qrpc_client_destroy(qrpc_client_t cl) {
//   auto c = ClientLoop::FromHandle(cl);
//   c->Close();
//   delete c;
// }
// QRPC_BOOTSTRAP void qrpc_client_poll(qrpc_client_t cl) {
//   ClientLoop::FromHandle(cl)->Poll();
// }
// QRPC_BOOTSTRAP bool qrpc_client_connect(qrpc_client_t cl, const qrpc_addr_t *addr, const qrpc_clconf_t *conf) {
//   auto loop = ClientLoop::FromHandle(cl);
//   //we are not smart aleck and wanna use ipv4 if possible 
//   return loop->Resolve(AF_INET, addr->host, addr->port, conf);
// }
// QRPC_BOOTSTRAP qrpc_hdmap_t qrpc_client_hdmap(qrpc_client_t cl) {
//   return ClientLoop::FromHandle(cl)->mutable_handler_map()->ToHandle();
// }
// QRPC_BOOTSTRAP void qrpc_client_set_thread(qrpc_client_t cl) {
//   ClientLoop::FromHandle(cl)->set_main_thread();
// }
// QRPC_BOOTSTRAP bool qrpc_client_resolve_host(qrpc_client_t cl, int family_pref, const char *hostname, qrpc_on_resolve_host_t cb) {
//   return ClientLoop::FromHandle(cl)->Resolve(family_pref, hostname, cb);
// }
// QRPC_THREADSAFE const char *nq_ntop(const char *src, qrpc_size_t srclen, char *dst, qrpc_size_t dstlen) {
//   if (AsyncResolver::NtoP(src, srclen, dst, dstlen) < 0) {
//     return nullptr;
//   } else {
//     return dst;
//   }
// }




// // --------------------------
// //
// // server API
// //
// // --------------------------
// QRPC_THREADSAFE qrpc_svconf_t qrpc_server_conf() {
//   qrpc_svconf_t conf = {
//     //cert cache size. default 16 and how meny sessions accepted per loop. default 1024
//     .quic_cert_cache_size = 0, .accept_per_loop = 0,
//     //allocation hint about max sessoin and max stream
//     .max_session_hint = 0, .max_stream_hint = 0,
//     //if set to true, max_session_hint will be hard limit
//     .use_max_session_hint_as_limit = false,
//     //total server shutdown wait/retry token timeout. default 5sec/30sec
//     //secret to generate retry token will be rotated every retry_token_timeout*2 seconds.
//     .shutdown_timeout = 0ULL, .retry_token_timeout = 30ULL,
//     //transport config see DefaultTransportConfig for default configs
//     .transport = DefaultTransportConfig()
//   };
//   qrpc_closure_init_noop(conf.on_open, qrpc_on_server_conn_open_t);
//   qrpc_closure_init_noop(conf.on_close, qrpc_on_server_conn_close_t);
//   return conf;
// }
// QRPC_THREADSAFE qrpc_server_t qrpc_server_create(int n_worker) {
//   lib_init(false); //anchor
//   auto sv = new Server(n_worker);
//   return sv->ToHandle();
// }
// qrpc_hdmap_t qrpc_server_listen(qrpc_server_t sv, const qrpc_addr_t *addr, const qrpc_svconf_t *conf) {
//   auto psv = Server::FromHandle(sv);
//   return psv->Open(addr, conf)->ToHandle();
// }
// QRPC_BOOTSTRAP void qrpc_server_start(qrpc_server_t sv, bool block) {
//   auto psv = Server::FromHandle(sv);
//   psv->Start(block);
// }
// QRPC_BOOTSTRAP void qrpc_server_join(qrpc_server_t sv) {
//   auto psv = Server::FromHandle(sv);
//   psv->Join();
//   delete psv;
// }



// // --------------------------
// //
// // hdmap API
// //
// // --------------------------
// QRPC_BOOTSTRAP bool qrpc_hdmap_stream_handler(qrpc_hdmap_t h, const char *name, qrpc_stream_handler_t handler) {
//   return HandlerMap::FromHandle(h)->AddEntry(name, handler);
// }
// QRPC_BOOTSTRAP bool qrpc_hdmap_rpc_handler(qrpc_hdmap_t h, const char *name, qrpc_rpc_handler_t handler) {
//   return HandlerMap::FromHandle(h)->AddEntry(name, handler);
// }
// QRPC_BOOTSTRAP bool qrpc_hdmap_stream_factory(qrpc_hdmap_t h, const char *name, qrpc_stream_factory_t factory) {
//   return HandlerMap::FromHandle(h)->AddEntry(name, factory);
// }
// QRPC_BOOTSTRAP void qrpc_hdmap_raw_handler(qrpc_hdmap_t h, qrpc_stream_handler_t handler) {
//   HandlerMap::FromHandle(h)->SetRawHandler(handler);
// }


// // --------------------------
// //
// // conn API
// //
// // --------------------------
// QRPC_THREADSAFE bool qrpc_conn_app_proto(qrpc_conn_t conn, const uint8_t **pp_proto, qrpc_size_t *p_proto_len) {
//   return SessionDelegate::FromHandle(conn)->GetAppProto(pp_proto, p_proto_len);
// }
// QRPC_THREADSAFE void qrpc_conn_close_ex(qrpc_conn_t conn, qrpc_close_reason_code_t code, const uint8_t *detail, qrpc_size_t detail_len) {
//   Unwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, ToConn(conn), Boxer::OpCode::Disconnect, code, detail, detail_len);
// }
// QRPC_THREADSAFE void qrpc_conn_reset(qrpc_conn_t conn) {
//   Unwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, ToConn(conn), Boxer::OpCode::Reconnect);
// } 
// QRPC_THREADSAFE void qrpc_conn_flush(qrpc_conn_t conn) {
//   Unwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, ToConn(conn), Boxer::OpCode::Flush);
// } 
// QRPC_THREADSAFE bool qrpc_conn_is_client(qrpc_conn_t conn) {
//   return Serial::IsClient(conn.s);
// }
// QRPC_THREADSAFE bool qrpc_conn_is_valid(qrpc_conn_t conn, qrpc_on_conn_validate_t cb) {
//   SessionDelegate *d;
//   UNWRAP_CONN(conn, d, {
//     no_ret_closure_call_with_check(cb, conn, nullptr);
//     return true;
//   }, "nq_conn_is_valid");
//   no_ret_closure_call_with_check(cb, conn, INVALID_REASON(conn));
//   return false;
// }
// QRPC_THREADSAFE void qrpc_conn_modify_hdmap(qrpc_conn_t conn, qrpc_on_conn_modify_hdmap_t modifier) {
//   SessionDelegate *d;
//   Boxer *b;
//   UNWRAP_CONN_OR_ENQUEUE(conn, d, b, {
//     auto hm = d->ResetHandlerMap()->ToHandle();
//     qrpc_closure_call(modifier, hm);
//   }, {
//     b->InvokeConn(conn.s, ToConn(conn), Boxer::OpCode::ModifyHandlerMap, qrpc_to_dyn_closure(modifier));
//   }, "nq_conn_modify_hdmap");
// }
// QRPC_THREADSAFE qrpc_time_t qrpc_conn_reconnect_wait(qrpc_conn_t conn) {
//   SessionDelegate *d;
//   UNWRAP_CONN(conn, d, {
//     return qrpc_time_usec(d->ReconnectDurationUS());
//   }, "nq_conn_reconnect_wait");
//   return 0;
// }
// QRPC_CLOSURECALL void *nq_conn_ctx(qrpc_conn_t conn) {
//   SessionDelegate *d;
//   UNSAFE_UNWRAP_CONN(conn, d, {
//     return d->Context();
//   }, "nq_conn_ctx");
//   return nullptr;
// }
// //these are hidden API for test, because returned value is unstable
// //when used with client connection (under reconnection)
// QRPC_THREADSAFE qrpc_cid_t qrpc_conn_id(qrpc_conn_t conn) {
//   SessionDelegate *d;
//   UNWRAP_CONN(conn, d, {
//     return d->ConnectionId();
//   }, "nq_conn_id");
//   return 0;
// }
// QRPC_THREADSAFE void qrpc_conn_reachability_change(qrpc_conn_t conn, qrpc_reachability_t state) {
//   Unwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, ToConn(conn), Boxer::OpCode::Reachability, state);
// }
// QRPC_THREADSAFE int qrpc_conn_fd(qrpc_conn_t conn) {
//   SessionDelegate *d;
//   UNWRAP_CONN(conn, d, {
//     return d->UnderlyingFd();
//   }, "nq_conn_fd");
//   return -1; 
// }


// // --------------------------
// //
// // stream API
// //
// // --------------------------
// static inline void conn_stream_common(qrpc_conn_t conn, const char *name, void *ctx, const char *purpose) {
//   SessionDelegate *d;
//   UNWRAP_CONN(conn, d, ({
//     // TODO(iyatomi): if possible, make this real thread safe
//     d->InitStream(name, ctx);
//   }), purpose);
// }


// QRPC_CLOSURECALL void qrpc_conn_stream(qrpc_conn_t conn, const char *name, void *ctx) {
//   conn_stream_common(conn, name, ctx, "nq_conn_stream");
// }
// QRPC_THREADSAFE qrpc_conn_t qrpc_stream_conn(qrpc_stream_t s) {
//   Stream *st;
//   UNWRAP_STREAM(s, st, ({
//     return Unwrapper::Stream2Conn(s.s, st);
//   }), "nq_stream_conn");
//   return INVALID_HANDLE<qrpc_conn_t>(IHR_CONN_NOT_FOUND);
// }
// QRPC_CLOSURECALL qrpc_alarm_t qrpc_stream_alarm(qrpc_stream_t s) {
//   // TODO(iyatomi): if possible, make this real thread safe
//   return Unwrapper::UnwrapBoxer(s)->NewAlarm()->ToHandle();
// }
// QRPC_THREADSAFE bool qrpc_stream_is_valid(qrpc_stream_t s, qrpc_on_stream_validate_t cb) {
//   Stream *st;
//   UNWRAP_STREAM(s, st, {
//     no_ret_closure_call_with_check(cb, s, nullptr);
//     return true;
//   }, "nq_stream_is_valid");
//   no_ret_closure_call_with_check(cb, s, INVALID_REASON(s));
//   return false;
// }
// QRPC_THREADSAFE bool qrpc_stream_outgoing(qrpc_stream_t s, bool *p_valid) {
//   Stream *st;
//   UNWRAP_STREAM(s, st, {
//     *p_valid = true;
//     return IsOutgoing(Serial::IsClient(s.s), st->id());
//   }, "nq_stream_close");
//   *p_valid = false;
//   return false;
// }
// QRPC_THREADSAFE void qrpc_stream_close(qrpc_stream_t s) {
//   Unwrapper::UnwrapBoxer(s)->InvokeStream(s.s, ToStream(s), Boxer::OpCode::Disconnect);
// }
// QRPC_THREADSAFE void qrpc_stream_send(qrpc_stream_t s, const void *data, qrpc_size_t datalen) {
//   Stream *st; Boxer *b;
//   UNWRAP_STREAM_OR_ENQUEUE(s, st, b, {
//     st->Handler<NqStreamHandler>()->Send(data, datalen);
//   }, {
//     b->InvokeStream(s.s, st, Boxer::OpCode::Send, data, datalen);
//   }, "nq_stream_send");
// }
// QRPC_THREADSAFE void qrpc_stream_send_ex(qrpc_stream_t s, const void *data, qrpc_size_t datalen, qrpc_stream_opt_t *opt) {
//   Stream *st; Boxer *b;
//   UNWRAP_STREAM_OR_ENQUEUE(s, st, b, {
//     st->Handler<NqStreamHandler>()->SendEx(data, datalen, *opt);
//   }, {
//     b->InvokeStream(s.s, st, Boxer::OpCode::SendEx, data, datalen, *opt);
//   }, "nq_stream_send");
// }
// QRPC_THREADSAFE void qrpc_stream_task(qrpc_stream_t s, qrpc_on_stream_task_t cb) {
//   Unwrapper::UnwrapBoxer(s)->InvokeStream(s.s, ToStream(s), Boxer::OpCode::Task, qrpc_to_dyn_closure(cb));
// }
// QRPC_CLOSURECALL void *nq_stream_ctx(qrpc_stream_t s) {
//   Stream *st;
//   UNSAFE_UNWRAP_STREAM(s, st, {
//     return st->Context();
//   }, "nq_stream_ctx");
// }
// QRPC_THREADSAFE qrpc_sid_t qrpc_stream_sid(qrpc_stream_t s) {
//   Stream *st;
//   UNWRAP_STREAM(s, st, {
//     return st->id();
//   }, "nq_stream_sid");
//   return 0;
// }



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



// // --------------------------
// //
// // alarm API
// //
// // --------------------------
// QRPC_THREADSAFE void qrpc_alarm_set(qrpc_alarm_t a, qrpc_time_t invocation_ts, qrpc_on_alarm_t cb) {
//   Unwrapper::UnwrapBoxer(a)->InvokeAlarm(a.s, ToAlarm(a), Boxer::OpCode::Start, invocation_ts, cb);
// }
// QRPC_THREADSAFE void qrpc_alarm_destroy(qrpc_alarm_t a) {
//   Unwrapper::UnwrapBoxer(a)->InvokeAlarm(a.s, ToAlarm(a), Boxer::OpCode::Finalize);
// }
// QRPC_THREADSAFE bool qrpc_alarm_is_valid(qrpc_alarm_t a) {
//   //because memory pointed to a.p never returned to heap 
//   //(alarm memory is from pre-allocated block(by qrpc::Allocator), this check should work always.
//   auto p = static_cast<NqAlarm *>(a.p);
//   return p->alarm_serial() == a.s;
// }



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
