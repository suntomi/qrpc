#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#if defined(__cplusplus)
extern "C" {
#endif



// --------------------------
//
// Annotation
//
// --------------------------
//indicate this call only safe before/after main loop running, 
//which means call of qrpc_server_start or qrpc_client_poll.
//also not guaranteed to be safe from calling concurrently
#define QAPI_BOOTSTRAP extern
//indicate this call can be done concurrently, and works correctly
//our thread safe approach is like following:
//read operation: just checking object is valid by comparing serial in
//  C object (nq_conn/stream/alarm_t) and C++ object (Alaram/NqClient/NqServerSession/NqStream)
//write operation: checking current thread is owner of objects (Alaram/NqClient/NqServerSession/NqStream) and
//  owner => do operation directly
//  non-owner => add operation to queue and owner thread process request
#define QAPI_THREADSAFE extern
//indicate this call only safe when invoked with nq_conn/rpc/stream_t which passed to
//functions of closure type (declared with QRPC_DECL_CLOSURE). 
#define QAPI_CLOSURECALL extern
//inline function
#define QAPI_INLINE static inline



// --------------------------
//
// Constants
//
// --------------------------
//alpn list
#define QRPC_ALPN ("\x2nq")
#define QRPC_QRPC_ALPN ("\x4qrpc")
#define QRPC_H3_ALPN ("\x2h3\x5h3-29\x5h3-30\x5h3-31\x5h3-32")
#define QRPC_H09_ALPN ("\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9")



// --------------------------
//
// Base type
//
// --------------------------
typedef size_t qrpc_size_t;

typedef int64_t qrpc_ssize_t;

typedef uint64_t qrpc_cid_t;  //connection id. this is not source/destination connection id of QUIC.

typedef uint64_t qrpc_sid_t;  //stream id

typedef uint64_t qrpc_time_t; //nano seconds timestamp

typedef time_t qrpc_unix_time_t; //place holder for unix timestamp

typedef uint32_t qrpc_msgid_t;

typedef uint32_t qrpc_stream_id_t;

typedef uint64_t qrpc_close_reason_code_t;

typedef struct qrpc_client_tag *qrpc_client_t; //NqClientLoop

typedef struct qrpc_server_tag *qrpc_server_t; //NqServer

typedef struct qrpc_hdmap_tag *qrpc_hdmap_t; //HandlerMap

typedef struct {
  uint64_t data[1];
} qrpc_serial_t;

typedef struct qrpc_conn_tag {
    qrpc_serial_t s; //see ConnSerialCodec
    void *p;    //NqSessionDelegate
} qrpc_conn_t;

typedef struct qrpc_stream_tag {
    qrpc_serial_t s; //see StreamSerialCodec
    void *p;    //NqStream
} qrpc_stream_t; 
//this is essentially same as nq_stream, but would be helpful to prevent misuse of rpc/stream
typedef struct qrpc_rpc_tag {
    qrpc_serial_t s; //see StreamSerialCodec
    void *p;    //NqStream
} qrpc_rpc_t; 

typedef struct qrpc_alarm_tag {
  qrpc_serial_t s;   //see AlarmSerialCodec
  void *p;      //NqAlarm
} qrpc_alarm_t;

typedef struct qrpc_transport_config_tag {
  //applicaiton protocol (ALPN) data, if you want to use the library
  //for implementing http3 server/client, you should set here the value `QRPC_H3_ALPN`
  const char *alpn;

  //path to session file (psk) for session resumption/0rtt. only client uses the value.
  //client loads session data from the file, but do not try resumption/0rtt if file does not exist.
  //and update file contents with its session data if connection established.
  //typical usage is, specify path that read/write-able for your app and let the library
  //read/write file to reduce handshake latency.
  const char *session_file;

  //enable early data in handshake. need session_file existence to take effect.
  //if enable_earty_data is true for qrpc_clconf_t, on_open will be called immediately when corresponding qrpc_conn_t is created.
  //it can call same API like qrpc_conn_(stream|rpc), qrpc_(stream|rpc)_send, as if on_open is called after handshake finish.
  //these call will generate early_data for 0rtt handshake and sent with Initial packet of QUIC, which reduces initial 0.5RTT latency.
  //but instead of lower latency, setting this true introduce some vulnerability (eg. for packet replay attack), 
  //you need to understand pros/cons of the option before set it true.
  bool enable_earty_data;

  //udp payload size in bytes
  qrpc_size_t udp_payload_size;

  //initial max data size in bytes.
  qrpc_size_t initial_max_data;
  qrpc_size_t initial_max_data_bidi_local;
  qrpc_size_t initial_max_data_bidi_remote;

  //initial max number of stream
  qrpc_size_t initial_max_stream_bidi;

  //total handshake time limit / no input limit. default 1000ms/500ms
  qrpc_time_t handshake_timeout, idle_timeout;

  //length of source connection id in bytes
  qrpc_size_t source_connection_id_length;
} qrpc_transport_config_t;

typedef enum {
  QRPC_OK = 0,
  QRPC_ESYSCALL = -1,
  QRPC_ETIMEOUT = -2,
  QRPC_EALLOC = -3,
  QRPC_ENOTSUPPORT = -4,
  QRPC_EGOAWAY = -5,
  QRPC_EDEPS = -6,    //dependent library error
  QRPC_EUSER = -7,    //for rpc, user calls qrpc_rpc_error to reply
  QRPC_ERESOLVE = -8, //address resolve error
  QRPC_EINVAL = -9,   //invalid parameter specified
  QRPC_EAGAIN = -10,  //temporary failure. retry required
  QRPC_ESIZE = -11,   //not enough size
} qrpc_error_t;

typedef struct {
  bool app;                     //error is initiated by app close frame?
  qrpc_close_reason_code_t code;  //explanation numeric error code
  const char *msg;              //explanation message (can be any binary data other than text)
  qrpc_size_t msglen;             //message length
} qrpc_close_reason_t;

QAPI_THREADSAFE const char *nq_error_detail_code2str(qrpc_error_t code, int detail_code);

typedef struct {
  const char *host, *cert, *key, *ca;
  int port;
} qrpc_addr_t;

typedef enum {
  QRPC_NOT_REACHABLE = 0,
  QRPC_REACHABLE_WIFI = 2,
  QRPC_REACHABLE_WWAN = 1,
} qrpc_reachability_t;

typedef enum {
  QRPC_WIRE_PROTO_QUIC_NEGOTIATE, // only client side
  QRPC_WIRE_PROTO_QUIC_V1,
  QRPC_WIRE_PROTO_TCP // not supported now
} qrpc_wire_proto_t;

// --------------------------
//
// Closure type
//
// --------------------------
//this means callback of __typename is __return_type (*)(...). 
//first void* argument of callback will always be __typename::arg. 
#define QRPC_DECL_CLOSURE(__return_type, __typename, ...) \
  typedef __return_type (*__typename##_proc)(__VA_ARGS__); \
  typedef struct { \
    void *arg; \
    __typename##_proc proc; \
  } __typename

#define QRPC_ALIAS_CLOSURE(__source_type, __dest_type) \
  typedef __source_type __dest_type; \
  typedef __source_type##_proc __dest_type##_proc

/* client */
//receive client handshake progress and done event. note that this usucally called twice to establish connection.
//optionally you can set arbiter pointer via last argument, which can be retrieved via qrpc_conn_ctx afterward.
//TODO(iyatomi): give more imformation for deciding shutdown connection from qrpc_conn_t
//TODO(iyatomi): re-evaluate we should call this twice (now mainly because to make open/close callback surely called as pair)
QRPC_DECL_CLOSURE(void, qrpc_on_client_conn_open_t, void *, qrpc_conn_t, void **);
//client connection closed. after this called, qrpc_stream_t/qrpc_rpc_t created by given qrpc_conn_t, will be invalid.
//last boolean indicates connection is closed from local(false) or remote(true).
//if this function returns positive value,
//connection automatically reconnect with back off which equals to returned value.
QRPC_DECL_CLOSURE(qrpc_time_t, qrpc_on_client_conn_close_t, void *, qrpc_conn_t, qrpc_error_t, const qrpc_close_reason_t*, bool);
//client connection finalized. just after this callback is done, memory corresponding to the qrpc_conn_t, will be freed. 
//because qrpc_conn_t is already invalidate when this callback invokes, almost qrpc_conn_* API returns invalid value in this callback.
//so the callback is basically for cleanup user defined resourse, like closure arg pointer (1st arg) or user context (3rd arg).
QRPC_DECL_CLOSURE(void, qrpc_on_client_conn_finalize_t, void *, qrpc_conn_t, void *);


/* server */
//server connection opened. same as qrpc_on_client_conn_open_t.
QRPC_ALIAS_CLOSURE(qrpc_on_client_conn_open_t, qrpc_on_server_conn_open_t);
//server connection closed. same as qrpc_on_client_conn_close_t but no reconnection feature
QRPC_DECL_CLOSURE(void, qrpc_on_server_conn_close_t, void *, qrpc_conn_t, qrpc_error_t, const qrpc_close_reason_t*, bool);


/* conn */
//called as 2nd argument qrpc_conn_valid, when actually given conn is valid.
QRPC_DECL_CLOSURE(void, qrpc_on_conn_validate_t, void *, qrpc_conn_t, const char *);
//called when qrpc_conn_modify_hdmap invoked with valid qrpc_conn_t
QRPC_DECL_CLOSURE(void, qrpc_on_conn_modify_hdmap_t, void *, qrpc_hdmap_t);


/* stream */
//stream opened. return false to reject stream
QRPC_DECL_CLOSURE(bool, qrpc_on_stream_open_t, void *, qrpc_stream_t, void**);
//stream closed. after this called, qrpc_stream_t which given to this function will be invalid.
QRPC_DECL_CLOSURE(void, qrpc_on_stream_close_t, void *, qrpc_stream_t);

QRPC_DECL_CLOSURE(void*, qrpc_stream_reader_t, void *, qrpc_stream_t, const char *, qrpc_size_t, int *);
//need to return pointer to serialized byte array via last argument. 
//memory for byte array owned by callee and have to be available until next call of this callback.
QRPC_DECL_CLOSURE(qrpc_size_t, qrpc_stream_writer_t, void *, qrpc_stream_t, const void *, qrpc_size_t, void **);

QRPC_DECL_CLOSURE(void, qrpc_on_stream_record_t, void *, qrpc_stream_t, const void *, qrpc_size_t);

QRPC_DECL_CLOSURE(void, qrpc_on_stream_task_t, void *, qrpc_stream_t);

QRPC_DECL_CLOSURE(void, qrpc_on_stream_ack_t, void *, int, qrpc_time_t);

QRPC_DECL_CLOSURE(void, qrpc_on_stream_retransmit_t, void *, int);
//called as 2nd argument qrpc_stream_valid, when actually given stream is valid.
QRPC_DECL_CLOSURE(void, qrpc_on_stream_validate_t, void *, qrpc_stream_t, const char *);

QRPC_DECL_CLOSURE(void*, qrpc_stream_factory_t, void *, qrpc_conn_t);


/* rpc */
//rpc opened. return false to reject rpc
QRPC_DECL_CLOSURE(bool, qrpc_on_rpc_open_t, void *, qrpc_rpc_t, void**);
//rpc closed. after this called, qrpc_stream_t which given to this function will be invalid.
QRPC_DECL_CLOSURE(void, qrpc_on_rpc_close_t, void *, qrpc_rpc_t);

QRPC_DECL_CLOSURE(void, qrpc_on_rpc_request_t, void *, qrpc_rpc_t, uint16_t, qrpc_msgid_t, const void *, qrpc_size_t);

QRPC_DECL_CLOSURE(void, qrpc_on_rpc_notify_t, void *, qrpc_rpc_t, uint16_t, const void *, qrpc_size_t);

QRPC_DECL_CLOSURE(void, qrpc_on_rpc_reply_t, void *, qrpc_rpc_t, qrpc_error_t, const void *, qrpc_size_t);

QRPC_DECL_CLOSURE(void, qrpc_on_rpc_task_t, void *, qrpc_rpc_t);
//called as 2nd argument qrpc_stream_valid, when actually given stream is valid.
QRPC_DECL_CLOSURE(void, qrpc_on_rpc_validate_t, void *, qrpc_rpc_t, const char *);


/* alarm */
QRPC_DECL_CLOSURE(void, qrpc_on_alarm_t, void *, qrpc_time_t *);


/* reachability */
QRPC_DECL_CLOSURE(void, qrpc_on_reachability_change_t, void *, qrpc_reachability_t);


/* resolver */
QRPC_DECL_CLOSURE(void, qrpc_on_resolve_host_t, void *, qrpc_error_t, const qrpc_close_reason_t *, const char *, qrpc_size_t);


/* macro */
#define qrpc_closure_is_empty(clsr) ((clsr).proc == nullptr)

#define qrpc_closure_empty() {nullptr, nullptr}

#define qrpc_closure_init(__pclsr, __cb, __arg) { \
  (__pclsr).arg = (void *)(__arg); \
  (__pclsr).proc = (__cb); \
}

QAPI_INLINE void *qrpc_closure_proc_generic_noop(...) { return nullptr; }
#define qrpc_closure_init_noop(__pclsr, __typename) { \
  (__pclsr).arg = nullptr; \
  (__pclsr).proc = (__typename##_proc)(qrpc_closure_proc_generic_noop); \
}

#define qrpc_closure_call(__pclsr, ...) ((__pclsr).proc((__pclsr).arg, __VA_ARGS__))



// --------------------------
//
// client API
//
// --------------------------
typedef struct {
  //dns query timeout in nsec
  qrpc_time_t query_timeout;

  //array of ipv4 or ipv6 ip address literal and its size.
  //first entry will be used first. if use_round_robin set to false
  //otherwise used one by one.
  //set null to dns_hosts to use default dns (for now 8.8.8.8)
  struct {
    const char *addr;
    int port; 
  } *dns_hosts;
  int n_dns_hosts;
  bool use_round_robin;
} qrpc_dns_conf_t;
typedef struct {
  //connection open/close/finalize watcher
  qrpc_on_client_conn_open_t on_open;
  qrpc_on_client_conn_close_t on_close;
  qrpc_on_client_conn_finalize_t on_finalize;

  //protocol type/version
  qrpc_wire_proto_t protocol;

  //transport config
  qrpc_transport_config_t transport;

  //set true to ignore proof verification
  bool insecure; 

  //track reachability to the provide hostname and recreate socket if changed.
  //useful for mobile connection. currently iOS only. use qrpc_conn_reachability_change for android.
  bool track_reachability;
} qrpc_clconf_t;

// get default qrpc_clconf_t
QAPI_THREADSAFE qrpc_clconf_t qrpc_client_conf();
// create client object which have max_nfd of connection. 
QAPI_BOOTSTRAP qrpc_client_t qrpc_client_create(int max_nfd, int max_stream_hint, const qrpc_dns_conf_t *dns_conf);
// do actual network IO. need to call periodically
QAPI_BOOTSTRAP void qrpc_client_poll(qrpc_client_t cl);
// close connections and destroy client object. after call this, do not call qrpc_client_* API.
QAPI_BOOTSTRAP void qrpc_client_destroy(qrpc_client_t cl);
// create conn from client. can get qrpc_conn_t via argument of qrpc_clconf_t::on_open
// return false conn on error. TODO(iyatomi): make it QAPI_THREADSAFE
QAPI_BOOTSTRAP bool qrpc_client_connect(qrpc_client_t cl, const qrpc_addr_t *addr, const qrpc_clconf_t *conf);
// get handler map of the client. 
QAPI_BOOTSTRAP qrpc_hdmap_t qrpc_client_hdmap(qrpc_client_t cl);
// set thread id that calls qrpc_client_poll.
// call this if thread which polls this qrpc_client_t is different from creator thread.
QAPI_BOOTSTRAP void qrpc_client_set_thread(qrpc_client_t cl);
// resolve host. qrpc_client_t need to be polled by qrpc_client_poll to work correctly
// family_pref can be AF_INET or AF_INET6, and control which address family searched first. 
QAPI_BOOTSTRAP bool qrpc_client_resolve_host(qrpc_client_t cl, int family_pref, const char *hostname, qrpc_on_resolve_host_t cb);
// for subsequent use of qrpc_client_resolve_host. passing 3rd and 4th argument of qrpc_on_resolve_host_t to this function,
// as src and srcsz. and passing buffer for dst and dstsz, to store string converted result of src/srcsz.
// return dst if succeed otherwise nullptr returned.
QAPI_THREADSAFE const char *nq_ntop(const char *src, qrpc_size_t srcsz, char *dst, qrpc_size_t dstsz);



// --------------------------
//
// server API
//
// --------------------------
typedef struct {
  //connection open/close watcher
  qrpc_on_server_conn_open_t on_open;
  qrpc_on_server_conn_close_t on_close;

  //transport config
  qrpc_transport_config_t transport;

  //cert cache size. default 16 and how meny sessions accepted per loop. default 1024
  int quic_cert_cache_size, accept_per_loop;

  //allocation hint about max sessoin and max stream
  int max_session_hint, max_stream_hint;

  //if set to true, max_session_hint will be hard limit
  bool use_max_session_hint_as_limit;

  //total server shutdown wait/retry token timeout. default 5sec/30sec
  //secret to generate retry token will be rotated every retry_token_timeout*2 seconds.
  qrpc_time_t shutdown_timeout, retry_token_timeout;
} qrpc_svconf_t;

// get default qrpc_svconf_t
QAPI_THREADSAFE qrpc_svconf_t qrpc_server_conf();
//create server which has n_worker of workers
QAPI_BOOTSTRAP qrpc_server_t qrpc_server_create(int n_worker);
//listen and returns handler map associated with it. 
QAPI_BOOTSTRAP qrpc_hdmap_t qrpc_server_listen(qrpc_server_t sv, const qrpc_addr_t *addr, const qrpc_svconf_t *config);
//if block is true, qrpc_server_start blocks until some other thread calls qrpc_server_join. 
QAPI_BOOTSTRAP void qrpc_server_start(qrpc_server_t sv, bool block);
//request shutdown and wait for server to stop. after calling this API, do not call qrpc_server_* API
QAPI_BOOTSTRAP void qrpc_server_join(qrpc_server_t sv);



// --------------------------
//
// hdmap API
//
// --------------------------
typedef struct {
  qrpc_on_stream_record_t on_stream_record;
  qrpc_on_stream_open_t on_stream_open;
  qrpc_on_stream_close_t on_stream_close;
  qrpc_stream_reader_t stream_reader;
  qrpc_stream_writer_t stream_writer;
} qrpc_stream_handler_t;

typedef struct {
  qrpc_on_rpc_request_t on_rpc_request;
  qrpc_on_rpc_notify_t on_rpc_notify;
  qrpc_on_rpc_open_t on_rpc_open;
  qrpc_on_rpc_close_t on_rpc_close;
  qrpc_time_t timeout; //call timeout
  bool use_large_msgid; //use 4byte for msgid
} qrpc_rpc_handler_t;

//setup original stream protocol (client), with 3 pattern
QAPI_BOOTSTRAP bool qrpc_hdmap_stream_handler(qrpc_hdmap_t h, const char *name, qrpc_stream_handler_t handler);

QAPI_BOOTSTRAP bool qrpc_hdmap_rpc_handler(qrpc_hdmap_t h, const char *name, qrpc_rpc_handler_t handler);

QAPI_BOOTSTRAP bool qrpc_hdmap_stream_factory(qrpc_hdmap_t h, const char *name, qrpc_stream_factory_t factory);
//if you call this API, qrpc_hdmap_t become "raw mode". any other hdmap settings are ignored, 
//and all incoming/outgoing streams are handled with the handler which is given to this API.
QAPI_BOOTSTRAP void qrpc_hdmap_raw_handler(qrpc_hdmap_t h, qrpc_stream_handler_t handler);



// --------------------------
//
// conn API
//
// --------------------------
//returns application protocol that nagotiated by ALPN
QAPI_THREADSAFE bool qrpc_conn_app_proto(qrpc_conn_t conn, const uint8_t **pp_proto, qrpc_size_t *p_proto_len);
//can modify handler map of connection, which is usually inherit from qrpc_client_t or qrpc_server_t.
//if you use this API in callback functions of qrpc_conn_t (eg. qrpc_on_client/server_conn_open_t) are called, 
//all modification of hdmap will be immediately finished.
//but if it called from outside of callback functions for qrpc_conn_t, it will be queued
//and actual modification will not immediately take place.
QAPI_THREADSAFE void qrpc_conn_modify_hdmap(qrpc_conn_t conn, qrpc_on_conn_modify_hdmap_t modifier);
//close connection with reason_code and reason_detail through close frame.
//close and destroy conn/associated stream eventually, so never touch conn/stream/rpc after calling this API
QAPI_THREADSAFE void qrpc_conn_close_ex(qrpc_conn_t conn, qrpc_close_reason_code_t code, const uint8_t *detail, qrpc_size_t detail_len);
//same as qrpc_conn_close_ex but do not send reason code and detail
QAPI_INLINE void qrpc_conn_close(qrpc_conn_t conn) { qrpc_conn_close_ex(conn, 0, (const uint8_t *)"", 0); }
//this just restart connection, if connection not start, start it, otherwise close connection once, then start again.
//it never destroy connection itself, but associated stream/rpc all destroyed. (client only)
QAPI_THREADSAFE void qrpc_conn_reset(qrpc_conn_t conn); 
//flush buffered packets of all stream
QAPI_THREADSAFE void qrpc_conn_flush(qrpc_conn_t conn);
//check connection is client mode or not.
QAPI_THREADSAFE bool qrpc_conn_is_client(qrpc_conn_t conn);
//check conn is valid. invalid means fail to create or closed, or temporary disconnected (will reconnect soon).
//note that if (nq_conn_is_valid(...)) does not assure any safety of following operation, when multi threaded event loop runs
//you should give cb parameter with filling qrpc_on_conn_validate member, to operate this conn safety on validation success.
//you can pass qrpc_closure_empty() for qrpc_conn_is_valid, if you dont need to callback.
QAPI_THREADSAFE bool qrpc_conn_is_valid(qrpc_conn_t conn, qrpc_on_conn_validate_t cb);
//get reconnect wait duration in us. 0 means does not wait reconnection
QAPI_THREADSAFE qrpc_time_t qrpc_conn_reconnect_wait(qrpc_conn_t conn);
//get context, which is set at on_conn_open
QAPI_CLOSURECALL void *nq_conn_ctx(qrpc_conn_t conn);
//check equality of qrpc_conn_t.
QAPI_INLINE bool qrpc_conn_equal(qrpc_conn_t c1, qrpc_conn_t c2) { return c1.s.data[0] == c2.s.data[0] && (c1.s.data[0] == 0 || c1.p == c2.p); }
//manually set reachability change for current connection
QAPI_THREADSAFE void qrpc_conn_reachability_change(qrpc_conn_t conn, qrpc_reachability_t new_status);
//get fd attached to the conn. client conn returns dedicated fd for the connection,
//server side returns lister fd, which is shared among connections.sz
QAPI_THREADSAFE int qrpc_conn_fd(qrpc_conn_t conn);


// --------------------------
//
// stream API 
//
// --------------------------
typedef struct {
  qrpc_on_stream_ack_t on_ack;
  qrpc_on_stream_retransmit_t on_retransmit;
} qrpc_stream_opt_t;

//create single stream from conn, which has type specified by "name". need to use valid conn
//open callback of this stream handler will receive invalid stream and null **ppctx on error, 
//valid stream handler and **ppctx where *ppctx == ctx on success.
QAPI_CLOSURECALL void qrpc_conn_stream(qrpc_conn_t conn, const char *name, void *ctx);
//get parent conn from rpc. it is possible returned qrpc_conn_t already become invalid when this function is called from non-owner thread of s
QAPI_THREADSAFE qrpc_conn_t qrpc_stream_conn(qrpc_stream_t s);
//get alarm from stream
QAPI_CLOSURECALL qrpc_alarm_t qrpc_stream_alarm(qrpc_stream_t s);
//check stream is valid. note that if (nq_stream_is_valid(...)) does not assure any safety of following operation.
//you should give cb parameter with filling qrpc_on_stream_validate member, to operate this stream object safely on validation success.
//you can pass qrpc_closure_empty() for qrpc_conn_is_valid, if you dont need to callback.
QAPI_THREADSAFE bool qrpc_stream_is_valid(qrpc_stream_t s, qrpc_on_stream_validate_t cb);
//check stream is outgoing. otherwise incoming. optionally you can get stream is valid, via p_valid. 
//if p_valid returns true, means stream is incoming.
QAPI_THREADSAFE bool qrpc_stream_outgoing(qrpc_stream_t s, bool *p_valid);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
QAPI_THREADSAFE void qrpc_stream_close(qrpc_stream_t s);
//send arbiter byte array/arbiter object to stream peer. if you want ack for each send, use qrpc_stream_send_ex
QAPI_THREADSAFE void qrpc_stream_send(qrpc_stream_t s, const void *data, qrpc_size_t datalen);
//send arbiter byte array/arbiter object to stream peer, and can receive ack of it.
QAPI_THREADSAFE void qrpc_stream_send_ex(qrpc_stream_t s, const void *data, qrpc_size_t datalen, qrpc_stream_opt_t *opt);
//schedule execution of closure which is given to cb, will called with given s.
QAPI_THREADSAFE void qrpc_stream_task(qrpc_stream_t s, qrpc_on_stream_task_t cb);
//check equality of qrpc_stream_t.
QAPI_INLINE bool qrpc_stream_equal(qrpc_stream_t c1, qrpc_stream_t c2) { return c1.s.data[0] == c2.s.data[0] && (c1.s.data[0] == 0 || c1.p == c2.p); }
//get stream id. this may change as you re-created stream on reconnection. 
//useful if you need to give special meaning to specified stream_id, like http2 over quic
QAPI_THREADSAFE qrpc_sid_t qrpc_stream_sid(qrpc_stream_t s);
//get context, which is set at qrpc_conn_stream. only safe with qrpc_stream_t which passed to closure callbacks
QAPI_CLOSURECALL void *nq_stream_ctx(qrpc_stream_t s);



// --------------------------
//
// rpc API
//
// --------------------------
typedef struct {
  qrpc_on_rpc_reply_t callback;
  qrpc_time_t timeout;
} qrpc_rpc_opt_t;

//create single rpc object from conn, which has type specified by "name". need to use valid conn
//open callback of this rpc handler will receive invalid rpc and null **ppctx on error, 
//valid stream handler and **ppctx where *ppctx == ctx on success.
QAPI_CLOSURECALL void qrpc_conn_rpc(qrpc_conn_t conn, const char *name, void *ctx);
//get parent conn from rpc. it is possible returned qrpc_conn_t already become invalid when this function is called from non-owner thread of rpc
QAPI_THREADSAFE qrpc_conn_t qrpc_rpc_conn(qrpc_rpc_t rpc);
//get alarm from stream or rpc
QAPI_CLOSURECALL qrpc_alarm_t qrpc_rpc_alarm(qrpc_rpc_t rpc);
//check rpc is valid. note that if (nq_rpc_is_valid(...)) does not assure any safety of following operation.
//you should give cb parameter with filling qrpc_on_rpc_validate member, to operate this rpc object safely on validation success.
//you can pass qrpc_closure_empty() for qrpc_conn_is_valid, if you dont need to callback.
QAPI_THREADSAFE bool qrpc_rpc_is_valid(qrpc_rpc_t rpc, qrpc_on_rpc_validate_t cb);
//check rpc is outgoing. otherwise incoming. optionally you can get stream is valid, via p_valid. 
//if p_valid returns true, means stream is incoming.
QAPI_THREADSAFE bool qrpc_rpc_outgoing(qrpc_rpc_t s, bool *p_valid);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
QAPI_THREADSAFE void qrpc_rpc_close(qrpc_rpc_t rpc);
//send arbiter byte array or object to stream peer. type should be positive
QAPI_THREADSAFE void qrpc_rpc_call(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen, qrpc_on_rpc_reply_t on_reply);
//same as qrpc_rpc_call but can specify various options like per call timeout
QAPI_THREADSAFE void qrpc_rpc_call_ex(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen, qrpc_rpc_opt_t *opts);
//send arbiter byte array or object to stream peer, without receving reply. type should be positive
QAPI_THREADSAFE void qrpc_rpc_notify(qrpc_rpc_t rpc, int16_t type, const void *data, qrpc_size_t datalen);
//send reply of specified request. result >= 0, data and datalen is response, otherwise error detail
QAPI_THREADSAFE void qrpc_rpc_reply(qrpc_rpc_t rpc, qrpc_msgid_t msgid, const void *data, qrpc_size_t datalen);
//send error response to specified request. data and datalen is error detail
QAPI_THREADSAFE void qrpc_rpc_error(qrpc_rpc_t rpc, qrpc_msgid_t msgid, const void *data, qrpc_size_t datalen);
//schedule execution of closure which is given to cb, will called with given rpc.
QAPI_THREADSAFE void qrpc_rpc_task(qrpc_rpc_t rpc, qrpc_on_rpc_task_t cb);
//check equality of qrpc_rpc_t.
QAPI_INLINE bool qrpc_rpc_equal(qrpc_rpc_t c1, qrpc_rpc_t c2) { return c1.s.data[0] == c2.s.data[0] && (c1.s.data[0] == 0 || c1.p == c2.p); }
//get rpc id. this may change as you re-created rpc on reconnection.
//useful if you need to give special meaning to specified stream_id, like http2 over quic
QAPI_THREADSAFE qrpc_sid_t qrpc_rpc_sid(qrpc_rpc_t rpc);
//get context, which is set at qrpc_conn_rpc. only safe with qrpc_rpc_t which passed to closure callbacks
QAPI_CLOSURECALL void *nq_rpc_ctx(qrpc_rpc_t s);



// --------------------------
//
// time API
//
// --------------------------
QAPI_INLINE qrpc_time_t qrpc_time_sec(uint64_t n) { return ((n) * 1000 * 1000 * 1000); }

QAPI_INLINE qrpc_time_t qrpc_time_msec(uint64_t n) { return ((n) * 1000 * 1000); }

QAPI_INLINE qrpc_time_t qrpc_time_usec(uint64_t n) { return ((n) * 1000); }

QAPI_INLINE qrpc_time_t qrpc_time_nsec(uint64_t n) { return (n); }

QAPI_INLINE double qrpc_time_to_sec(qrpc_time_t n) { return ((n) / (1000 * 1000 * 1000)); }

QAPI_INLINE double qrpc_time_to_msec(qrpc_time_t n) { return ((n) / (1000 * 1000)); }

QAPI_INLINE double qrpc_time_to_usec(qrpc_time_t n) { return ((n) / 1000); }

QAPI_INLINE double qrpc_time_to_nsec(qrpc_time_t n) { return (n); }

QAPI_THREADSAFE qrpc_time_t qrpc_time_now();

QAPI_THREADSAFE qrpc_unix_time_t qrpc_time_unix();
//ignore EINTR
QAPI_THREADSAFE qrpc_time_t qrpc_time_sleep(qrpc_time_t d);
//break with EINTR
QAPI_THREADSAFE qrpc_time_t qrpc_time_pause(qrpc_time_t d);



// --------------------------
//
// alarm API
//
// --------------------------
#define STOP_INVOKE_QRPC_TIME (0)
//configure alarm to invoke cb after current time exceeds first, 
//at thread which handle receive callback of nq_rpc/stream_t that creates the alarm.
//if you set next invocation timestamp value(>= input value) to 3rd argument of cb, alarm scheduled to run that time, 
//if you set the value to 0(STOP_INVOKE_QRPC_TIME), it stopped (still valid and can reactivate with qrpc_alarm_set). 
//otherwise alarm remove its memory, further use of qrpc_alarm_t is not possible (silently ignored)
//suitable if you want to create some kind of poll method of your connection.
QAPI_THREADSAFE void qrpc_alarm_set(qrpc_alarm_t a, qrpc_time_t first, qrpc_on_alarm_t cb);
//destroy alarm. after call this, any attempt to call qrpc_alarm_set will be ignored.
QAPI_THREADSAFE void qrpc_alarm_destroy(qrpc_alarm_t a);
//check if alarm is valid
QAPI_THREADSAFE bool qrpc_alarm_is_valid(qrpc_alarm_t a);



// --------------------------
//
// log API
//
// --------------------------
//log handler. 
typedef void (*qrpc_logger_t)(const char *, size_t);
//log configuration
typedef struct {
  //(possibly) unique identifier of log stream which is created by single process
  const char *id;

  //if set to true, you need to call qrpc_log_flush() periodically to actually output logs.
  //useful for some environement like Unity Editor, which cannot call logging API from non-main thread.
  //https://fogbugz.unity3d.com/default.asp?949512_dab6v5ranqbebqr5
  bool manual_flush;

  //log handler
  qrpc_logger_t callback;
} qrpc_logconf_t;
//log severity
typedef enum {
  QRPC_LOGLV_TRACE,
  QRPC_LOGLV_DEBUG,
  QRPC_LOGLV_INFO,
  QRPC_LOGLV_WARN,
  QRPC_LOGLV_ERROR,
  QRPC_LOGLV_FATAL,            
  QRPC_LOGLV_REPORT, //intend to use for msg that is very important, but not error.
  QRPC_LOGLV_MAX,
} qrpc_loglv_t;
//structured log param
typedef enum {
  QRPC_LOG_INTEGER,
  QRPC_LOG_STRING,
  QRPC_LOG_DECIMAL,
  QRPC_LOG_BOOLEAN,
} qrpc_logparam_type_t;
typedef struct {
  const char *key;
  qrpc_logparam_type_t type;
  union {
    double d;
    uint64_t n;
    const char *s;
    bool b;
  } value;
} qrpc_logparam_t;

QAPI_BOOTSTRAP void qrpc_log_config(const qrpc_logconf_t *conf);
//write JSON structured log output. 
QAPI_THREADSAFE void nq_log(qrpc_loglv_t lv, const char *msg, qrpc_logparam_t *params, int n_params);
//write JSON Structured log output, with only msg
QAPI_INLINE void nq_msg(qrpc_loglv_t lv, const char *msg) { nq_log(lv, msg, NULL, 0); }
//flush cached log. only enable if you configure manual_flush to true. 
//recommend to call from only one thread. otherwise log output order may change from actual order.
QAPI_THREADSAFE void qrpc_log_flush(); 



#if defined(__cplusplus)
}
#endif
