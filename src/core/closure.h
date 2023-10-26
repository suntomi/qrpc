#pragma once

#include "qrpc.h"

/* closure */
typedef union {
  qrpc_on_client_conn_open_t on_client_conn_open;
  qrpc_on_client_conn_close_t on_client_conn_close;
  qrpc_on_client_conn_finalize_t on_client_conn_finalize;

  qrpc_on_server_conn_open_t on_server_conn_open;
  qrpc_on_server_conn_close_t on_server_conn_close;

  qrpc_on_conn_validate_t on_conn_validate;
  qrpc_on_conn_modify_hdmap_t on_conn_modify_hdmap;

  qrpc_on_stream_open_t on_stream_open;
  qrpc_on_stream_close_t on_stream_close;
  qrpc_stream_reader_t stream_reader;
  qrpc_stream_writer_t stream_writer;
  qrpc_on_stream_record_t on_stream_record;
  qrpc_on_stream_task_t on_stream_task;
  qrpc_on_stream_ack_t on_stream_ack;
  qrpc_on_stream_retransmit_t on_stream_retransmit;
  qrpc_on_stream_validate_t on_stream_validate;

  qrpc_on_rpc_open_t on_rpc_open;
  qrpc_on_rpc_close_t on_rpc_close;
  qrpc_on_rpc_request_t on_rpc_request;
  qrpc_on_rpc_reply_t on_rpc_reply;
  qrpc_on_rpc_notify_t on_rpc_notify;
  qrpc_on_rpc_task_t on_rpc_task;
  qrpc_on_rpc_validate_t on_rpc_validate;

  qrpc_stream_factory_t stream_factory;

  qrpc_on_alarm_t on_alarm;

  qrpc_on_reachability_change_t on_reachability_change;

  qrpc_on_resolve_host_t on_resolve_host;
} qrpc_closure_t;


#define qrpc_dyn_closure_init(__pclsr, __type, __cb, __arg) { \
  (__pclsr).__type.arg = (void *)(__arg); \
  (__pclsr).__type.proc = (__cb); \
}

#define qrpc_dyn_closure_call(__pclsr, __type, ...) ((__pclsr).__type.proc((__pclsr).__type.arg, __VA_ARGS__))

#define qrpc_dny_closure_empty() { { nullptr, nullptr } }

#define qrpc_to_dyn_closure(clsr) *((qrpc_closure_t *)(&(clsr)))