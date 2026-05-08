#include "qrpc.h"

#include <ares.h>
#include <pthread.h>
#include <unistd.h>

#include <array>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "base/logger.h"
#include "base/sig.h"

#include "base/defs.h"
#include "base/serial.h"
#include "base/timespec.h"

#include "json.hpp"
using json = nlohmann::json;

#include "qrpc/base.h"
#include "qrpc/conn.h"
#include "qrpc/loop.h"
#include "qrpc/sig.h"
#include "qrpc/transport.h"
#include "qrpc/worker.h"

#if defined(QRPC_THREADSAFE)
#undef QRPC_THREADSAFE
#define QRPC_THREADSAFE
#endif

#if defined(QRPC_INI_FINI)
#undef QRPC_INI_FINI
#define QRPC_INI_FINI
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
          .shutdown_timeout = qrpc_time_sec(3), .http_timeout = qrpc_time_sec(5),
          // consent check interval
          .consent_check_interval = qrpc_time_sec(10),
          // fingerprint algorithm of DTLS
          // any of "sha-1", "sha-224", "sha-256", "sha-384", "sha-512"
          .fingerprint_algorithm = "sha-256",
        },
        // rtp config
        .rtp = {
          .initial_outgoing_bitrate = 600000,
          .max_outgoing_bitrate = 0,
          .max_incoming_bitrate = 0,
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
// signal API
//
// --------------------------
QRPC_INI_FINI int qrpc_signal_init() {
  return qrpc::signal_init();
}
QRPC_THREADSAFE int qrpc_signal_handle(int signum, qrpc_signal_handler_t handler) {
  return qrpc::signal_handle(signum, handler);
}
QRPC_THREADSAFE int qrpc_signal_unhandle(int signum) {
  return qrpc::signal_unhandle(signum);
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
      .poll_interval = qrpc_time_msec(10),
      .use_dns = true, .use_hosts = true,
      .dns_hosts = nullptr,
      .n_dns_hosts = 0,
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
        .proto = QRPC_WEBRTC_ENDPOINT_PROTOCOL_ALL
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
  if (c->GetPartitionId() != Worker::g_partition_id()) {
    c->Enqueue([c, conf]() {
      c->Connect(*conf);
    });
    return;
  }
  //we are not smart aleck and wanna use ipv4 if possible
  c->Connect(*conf);
}
QRPC_THREADSAFE void qrpc_client_resolve(qrpc_client_t cl, int family_pref, const char *hostname, qrpc_on_resolve_host_t cb) {
  auto c = qrpc::Client::FromHandle(cl);
  if (c->GetPartitionId() != Worker::g_partition_id()) {
    std::string host = hostname;
    c->Enqueue([c, family_pref, host, cb]() {
      c->Resolve(family_pref, host, cb);
    });
    return;
  }
  c->Resolve(family_pref, hostname, cb);
}
QRPC_INI_FINI void qrpc_client_destroy(qrpc_client_t cl) {
  auto c = qrpc::Client::FromHandle(cl);
  c->Close();
  delete c;
}
QRPC_THREADSAFE void qrpc_client_poll(qrpc_client_t cl) {
  auto c = qrpc::Client::FromHandle(cl);
  if (UNLIKELY(c->GetPartitionId() != Worker::g_partition_id())) {
    logger::die({{"ev","qrpc_client_poll called from non-owner thread"},});
  }
  c->Poll();
}
QRPC_INI_FINI void qrpc_client_own(qrpc_client_t cl) {
  auto c = qrpc::Client::FromHandle(cl);
  if (c->GetPartitionId() == Worker::g_partition_id()) {
    return;
  }
  c->ResetPartition();
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
    //transport config see DefaultTransportConfig for default configss
    .transport = DefaultTransportConfig(s->transport_type()),
    //how meny sessions accepted per loop. default 1024
    .accept_per_loop = 0,
    //allocation hint about max session
    .max_session_hint = 0,
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
QRPC_INI_FINI int qrpc_server_listen(qrpc_server_t sv, const qrpc_listen_conf_t *conf) {
  auto s = Server::FromHandle(sv);
  return s->Open(*conf);
}
QRPC_INI_FINI void qrpc_server_start(qrpc_server_t sv, bool block) {
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
  if (partition_id != Worker::g_partition_id()) { \
    Worker::queue(partition_id).enqueue([__VA_ARGS__]() { \
      __proc; \
    }); \
  } else { \
    __proc; \
  } \
} while (0)
#define CONN_OP(__proc, ...) OP_RAW(do { \
  auto __c = qrpc::Connection::FromHandle(GET_FIRST(__VA_ARGS__)); \
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
QRPC_THREADSAFE void qrpc_conn_task(qrpc_conn_t conn, qrpc_on_conn_task_t cb) {
  OP_RAW(qrpc_closure_call(cb, conn);, conn, cb);
}
QRPC_CLOSURECALL bool qrpc_conn_is_client(qrpc_conn_t conn) {
  auto c = qrpc::Connection::FromHandle(conn);
  if (c != nullptr) {
    return c->is_client();
  }
  return false;
}
QRPC_CLOSURECALL bool qrpc_conn_is_valid(qrpc_conn_t conn) {
  return qrpc::Connection::FromHandle(conn) != nullptr;
}
QRPC_CLOSURECALL void *qrpc_conn_ctx(qrpc_conn_t conn) {
  auto c = qrpc::Connection::FromHandle(conn);
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
  auto __s = qrpc::Stream::FromHandle(GET_FIRST(__VA_ARGS__)); \
  if (__s != nullptr) { \
    __proc; \
  } \
} while(0), __VA_ARGS__)
#define GET_SECOND(__x, __y, ...) __y
#define GET_THIRD(__x, __y, __z, ...) __z
#define BYTES_OP(__proc, ...) do { \
  auto __h = GET_FIRST(__VA_ARGS__); \
  auto __ptr = GET_SECOND(__VA_ARGS__); \
  auto __size = GET_THIRD(__VA_ARGS__); \
  auto partition_id = base::Serial::GetPartitionId(__h.s); \
  if (partition_id != Worker::g_partition_id()) { \
    GET_SECOND(__VA_ARGS__) = base::Syscall::Memdup(__ptr, __size); \
    Worker::queue(partition_id).enqueue([__VA_ARGS__]() { \
      __proc; \
      base::Syscall::MemFree(const_cast<void *>(GET_SECOND(__VA_ARGS__))); \
    }); \
  } else { \
    __proc; \
  } \
} while (0)
#define STREAM_BYTES_OP(__proc, ...) BYTES_OP(do { \
  auto __s = qrpc::Stream::FromHandle(GET_FIRST(__VA_ARGS__)); \
  if (__s != nullptr) { \
    __proc; \
  } \
} while(0), __VA_ARGS__)
QRPC_THREADSAFE qrpc_stream_config_t qrpc_stream_conf(const char *name) {
  qrpc_stream_config_t conf = {
    .name = name,
    .stream_id = 0,
    .ordered = true,
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
  return dynamic_cast<qrpc::Connection &>(qrpc::Stream::FromHandle(s)->connection()).ToHandle();
}
QRPC_CLOSURECALL qrpc_alarm_t qrpc_stream_alarm(qrpc_stream_t s) {
  return qrpc::Stream::FromHandle(s)->connection().alarm_processor().ToHandle();
}
QRPC_CLOSURECALL bool qrpc_stream_is_valid(qrpc_stream_t s) {
  return qrpc::Stream::FromHandle(s) != nullptr;
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
  STREAM_BYTES_OP(__s->Send(static_cast<const char *>(data), datalen), s, data, datalen);
}
QRPC_THREADSAFE void qrpc_stream_task(qrpc_stream_t s, qrpc_on_stream_task_t cb) {
  STREAM_OP(qrpc_closure_call(cb, s), s, cb);
}
QRPC_CLOSURECALL void *qrpc_stream_ctx(qrpc_stream_t s) {
  return qrpc::Stream::FromHandle(s)->context_ptr();
}
QRPC_CLOSURECALL qrpc_sid_t qrpc_stream_sid(qrpc_stream_t s) {
  return qrpc::Stream::FromHandle(s)->id();
}



// --------------------------
//
// rpc API
//
// --------------------------
#define RPC_OP(__proc, ...) OP_RAW(do { \
  auto __r = qrpc::RPCBase::FromHandle(GET_FIRST(__VA_ARGS__)); \
  if (__r != nullptr) { \
    __proc; \
  } \
} while(0), __VA_ARGS__)
#define RPC_BYTES_OP(__proc, ...) BYTES_OP(do { \
  auto __r = qrpc::RPCBase::FromHandle(GET_FIRST(__VA_ARGS__)); \
  if (__r != nullptr) { \
    __proc; \
  } \
} while(0), __VA_ARGS__)

QRPC_CLOSURECALL void qrpc_conn_rpc(qrpc_conn_t conn, const qrpc_stream_config_t *conf, void *ctx) {
  auto cf = *conf;
  CONN_OP(__c->OpenStream(base::Stream::Config::From(cf, ctx));, conn, cf, ctx);
}
QRPC_THREADSAFE qrpc_conn_t qrpc_rpc_conn(qrpc_rpc_t rpc) {
  return dynamic_cast<qrpc::Connection &>(qrpc::Stream::FromHandle(rpc)->connection()).ToHandle();
}
QRPC_CLOSURECALL qrpc_alarm_t qrpc_rpc_alarm(qrpc_rpc_t rpc) {
  return qrpc::Stream::FromHandle(rpc)->connection().alarm_processor().ToHandle();
}
QRPC_THREADSAFE bool qrpc_rpc_is_valid(qrpc_rpc_t rpc) {
  return qrpc::Stream::FromHandle(rpc) != nullptr;
}
QRPC_THREADSAFE void qrpc_rpc_validate(qrpc_rpc_t rpc, qrpc_on_rpc_validate_t cb) {
  OP_RAW(qrpc_closure_call(cb, rpc, qrpc_rpc_is_valid(rpc)), rpc, cb);
}
QRPC_THREADSAFE void qrpc_rpc_close(qrpc_rpc_t rpc) {
  RPC_OP(__r->Close(base::Stream::CloseReason {
    .code = QRPC_CLOSE_REASON_LOCAL,
    .detail_code = 0,
    .msg = "closed by user",
  }), rpc);
}
QRPC_THREADSAFE void qrpc_rpc_call(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen, qrpc_on_rpc_reply_t on_reply) {
  RPC_BYTES_OP(
    __r->Call(type, static_cast<const char *>(data), datalen, on_reply),
    rpc, data, datalen, type, on_reply
  );
}
QRPC_THREADSAFE void qrpc_rpc_callx(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen, qrpc_rpc_opt_t *opts) {
  auto o = *opts;
  RPC_BYTES_OP(
    __r->CallEx(type, static_cast<const char *>(data), datalen, o),
    rpc, data, datalen, type, o
  );
}
QRPC_THREADSAFE void qrpc_rpc_notify(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen) {
  RPC_BYTES_OP(
    __r->Notify(type, static_cast<const char *>(data), datalen),
    rpc, data, datalen, type
  );
}
QRPC_CLOSURECALL void qrpc_rpc_reply(qrpc_rpc_t rpc, qrpc_msgid_t msgid, const void *data, qrpc_size_t datalen) {
  auto r = qrpc::RPCBase::FromHandle(rpc);
  r->Reply(QRPC_OK, msgid, static_cast<const char *>(data), datalen);
}
QRPC_CLOSURECALL void qrpc_rpc_error(qrpc_rpc_t rpc, qrpc_msgid_t msgid, qrpc_error_t error, const void *data, qrpc_size_t datalen) {
  auto r = qrpc::RPCBase::FromHandle(rpc);
  r->Reply(error, msgid, static_cast<const char *>(data), datalen);
}
QRPC_THREADSAFE void qrpc_rpc_task(qrpc_rpc_t rpc, qrpc_on_rpc_task_t cb) {
  RPC_OP(qrpc_closure_call(cb, rpc), rpc, cb);
}
QRPC_CLOSURECALL void *qrpc_rpc_ctx(qrpc_rpc_t rpc) {
  return qrpc::Stream::FromHandle(rpc)->context_ptr();
}
QRPC_CLOSURECALL qrpc_sid_t qrpc_rpc_sid(qrpc_rpc_t rpc) {
  return qrpc::Stream::FromHandle(rpc)->id();
}



// --------------------------
//
// media API
//
// --------------------------
#define MEDIA_OP(__proc, ...) OP_RAW(do { \
  auto __m = qrpc::Media::FromHandle(GET_FIRST(__VA_ARGS__)); \
  if (__m != nullptr) { \
    __proc; \
  } \
} while(0), __VA_ARGS__)
QRPC_THREADSAFE qrpc_media_config_t qrpc_media_config() {
  // default: opus
  static const char *audio_rtcp_fbs[] = {
    "transport-cc"
  };
  static qrpc_media_codec_t audio_codecs[] = {
    {
      .mime_type = "opus",
      .clock_rate = 48000,
      .payload_type = 111,
      .channels = 2,
      .fmtp = "minptime=10;useinbandfec=1",
      .n_rtcp_fbs = bulkof(audio_rtcp_fbs),
      .rtcp_fbs = audio_rtcp_fbs
    }
  };
  static qrpc_media_hdext_t audio_hdexts[] = {
    {.id = QRPC_RTP_HDEXT_SSRC_AUDIO_LEVEL, .uri = "urn:ietf:params:rtp-hdrext:ssrc-audio-level"},
    {.id = QRPC_RTP_HDEXT_ABS_SEND_TIME, .uri = "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"},
    {.id = QRPC_RTP_HDEXT_TRANSPORT_WIDE_CC_01, .uri = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"},
    {.id = QRPC_RTP_HDEXT_MID, .uri = "urn:ietf:params:rtp-hdrext:sdes:mid"},
  };
  static qrpc_media_encoding_t audio_encodings[] = {
    {
      .max_bitrate = 500000,
      .scalability_mode = nullptr,
      .ssrc = 0, .rtx_ssrc = 0,
      .payload_type = 111, .rtx_payload_type = 0,
      .dtx = true, .rtx = false,
    },
  };
  // default: VP8 simulcast
  static const char *video_rtcp_fbs[] = {
    "transport-cc", "goog-remb", "ccm fir", "nack", "nack pli"
  };
  static qrpc_media_codec_t video_codecs[] = {
    // a=rtpmap:96 VP8/90000
    {
      .mime_type = "VP8",
      .clock_rate = 90000,
      .payload_type = 96,
      .channels = 0,
      .fmtp = nullptr,
      .n_rtcp_fbs = bulkof(video_rtcp_fbs),
      .rtcp_fbs = video_rtcp_fbs
    },
    // a=rtpmap:97 rtx/90000
    // a=fmtp:97 apt=96
    {
      .mime_type = "rtx",
      .clock_rate = 90000,
      .payload_type = 97,
      .channels = 0,
      .fmtp = "apt=96",
      .n_rtcp_fbs = 0,
      .rtcp_fbs = {}
    }
  };
  static qrpc_media_hdext_t video_hdexts[] = {
    {.id = QRPC_RTP_HDEXT_TOFFSET, .uri = "urn:ietf:params:rtp-hdrext:toffset"},
    {.id = QRPC_RTP_HDEXT_ABS_SEND_TIME, .uri = "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"},
    {.id = QRPC_RTP_HDEXT_VIDEO_ORIENTATION, .uri = "urn:3gpp:video-orientation"},
    {.id = QRPC_RTP_HDEXT_TRANSPORT_WIDE_CC_01, .uri = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"},
    {.id = QRPC_RTP_HDEXT_PLAYOUT_DELAY, .uri = "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay"},
    {.id = QRPC_RTP_HDEXT_MID, .uri = "urn:ietf:params:rtp-hdrext:sdes:mid"},
    {.id = QRPC_RTP_HDEXT_STREAM_ID, .uri = "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id"},
    {.id = QRPC_RTP_HDEXT_REPAIRED_STREAM_ID, .uri = "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id"},
    {.id = QRPC_RTP_HDEXT_DEPENDENCY_DESCRIPTOR, .uri = "https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension"},
  };
  static qrpc_media_encoding_t video_encodings[] = {
    {
      .max_bitrate = 500000,
      .scalability_mode = "L1T3",
      .ssrc = 0, .rtx_ssrc = 0,
      .payload_type = 96, .rtx_payload_type = 97,
      .dtx = false, .rtx = true,
    },
    {
      .max_bitrate = 1000000,
      .scalability_mode = "L1T3",
      .ssrc = 0, .rtx_ssrc = 0,
      .payload_type = 96, .rtx_payload_type = 97,
      .dtx = false, .rtx = true,
    },
    {
      .max_bitrate = 5000000,
      .scalability_mode = "L1T3",
      .ssrc = 0, .rtx_ssrc = 0,
      .payload_type = 96, .rtx_payload_type = 97,
      .dtx = false, .rtx = true,
    },
  };
  return {
    .cname = nullptr,
    .audio_cap = {
      .n_codecs = bulkof(audio_codecs),
      .codecs = audio_codecs,
      .n_hdexts = bulkof(audio_hdexts),
      .hdexts = audio_hdexts,
      .n_encodings = bulkof(audio_encodings),
      .encodings = audio_encodings,
    },
    .video_cap = {
      .n_codecs = bulkof(video_codecs),
      .codecs = video_codecs,
      .n_hdexts = bulkof(video_hdexts),
      .hdexts = video_hdexts,
      .n_encodings = bulkof(video_encodings),
      .encodings = video_encodings,
    },
  };
}
QRPC_THREADSAFE void qrpc_conn_media_init(qrpc_conn_t c, const qrpc_media_config_t *config) {
  CONN_OP(__c->InitMedia(*config), c, config);
}
QRPC_THREADSAFE void qrpc_conn_media_open(qrpc_conn_t c, const qrpc_media_produce_config_t *config) {
  CONN_OP(__c->OpenMedia(*config), c, config);
}
QRPC_THREADSAFE void qrpc_conn_media_watch(qrpc_conn_t c, const qrpc_media_consume_config_t *config) {
  CONN_OP(__c->WatchMedia(*config), c, config);
}
QRPC_THREADSAFE void qrpc_media_watch(qrpc_media_t m, qrpc_on_media_consume_t cb) {
  MEDIA_OP(__m->SetWatcher(cb), m, cb);
}
QRPC_THREADSAFE void qrpc_media_close(qrpc_media_t m) {
  MEDIA_OP(__m->connection().CloseMedia(qrpc::Media::FromHandle(m)->path()), m);
}
QRPC_THREADSAFE void qrpc_media_control(qrpc_media_t m, const qrpc_media_control_t *control) {
  MEDIA_OP(__m->connection().ControlMedia(qrpc::Media::FromHandle(m)->path(), *control), m, control);
}
QRPC_CLOSURECALL bool qrpc_media_paused(qrpc_media_t m) {
  return qrpc::Media::FromHandle(m)->connection().IsMediaPaused(qrpc::Media::FromHandle(m)->path());
}
QRPC_CLOSURECALL const char *qrpc_media_path(qrpc_media_t m) {
  return qrpc::Media::FromHandle(m)->path().c_str();
}



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
QRPC_INI_FINI void qrpc_log_config(const qrpc_logconf_t *conf) {
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
