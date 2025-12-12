#pragma once

#include "base/webrtc.h"
#include "base/string.h"

#include "qrpc/base.h"
#include "qrpc/client.h"
#include "qrpc/listener.h"
#include "qrpc/server.h"
#include "qrpc/stream.h"

namespace qrpc {
using ListenerInterface = Listener;
using ClientInterface = Client;
namespace webrtc {
  using ConnectionFactory = base::webrtc::ConnectionFactory;
  using DtlsTransport = RTC::DtlsTransport;
  static inline qrpc_transport_type_t type() {
    return QRPC_TRANSPORT_WEBRTC;
  }

  // NewStream
  static inline Stream *NewStream(
    const Stream::Config &c, base::Connection &conn, const qrpc_handler_entry_t &he
  ) {
    Stream *s;
    switch (he.type) {
    case qrpc_handler_type_t::STREAM: {
      if (qrpc_closure_is_empty(he.stream.stream_reader)) {
        return new CodedByteStream(conn, c, he.stream);
      } else {
        return new RawByteStream(conn, c, he.stream); 
      }
    } break;
    case qrpc_handler_type_t::RPC: {
      return new RPCStream(conn, c, he.rpc, conn.alarm_processor());
    } break;
    default:
      ASSERT(false);
      return nullptr;
    }
  }

  // webrtc::ServerConnection
  class ServerConnection : public base::webrtc::Listener::Connection {
  public:
    ServerConnection(ConnectionFactory &cf, DtlsTransport::Role dtls_role, const qrpc_listen_conf_t &config) :
      Connection(cf, dtls_role), on_open_(config.on_open), on_close_(config.on_close), ctx_(nullptr) {}
    qrpc_conn_t ToHandle() { return { .p = this, .s = 0 }; }
    int OnConnect() override { return qrpc_closure_call(on_open_, ToHandle(), &ctx_); }
    qrpc_time_t OnShutdown() override { 
      qrpc_closure_call(on_close_, ToHandle(), &close_reason_->To(), &ctx_);
      return qrpc_alarm_stop_rv();
    }
  protected:
    void *ctx_;
    qrpc_on_server_conn_open_t on_open_;
    qrpc_on_server_conn_close_t on_close_;
  };

  // webrtc::Listener
  class Listener : public base::webrtc::Listener, public ListenerInterface {
  public:
    Listener(Worker &w, int port_index, const qrpc_listen_conf_t &config) :
      base::webrtc::Listener(
        w.loop(),
        // configurations
        base::webrtc::Listener::Config::From(
          config.transport.webrtc.params.session_timeout,
          config.transport.webrtc.params.connection_timeout
        ),
        base::webrtc::Listener::TransportConfig::From(config.transport),
        // connection factory method
        [this](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
          return new ServerConnection(cf, role, config_);
        },
        // stream factory
        [this](const Stream::Config &c, base::Connection &conn) {
          auto &sc = dynamic_cast<ServerConnection &>(conn);
          auto he = qrpc_closure_call(config_.stream_router, c.label.c_str(), sc.ToHandle());
          if (he == nullptr) {
            logger::die({{"ev","no handler found"},{"label",c.label},{"ptr",base::str::dptr(&conn)}});
          }
          return std::shared_ptr<Stream>(qrpc::webrtc::NewStream(c, conn, *he));
        }
      ), worker_(w), config_(config), port_index_(port_index) {}
    qrpc_transport_type_t transport_type() const override { return QRPC_TRANSPORT_WEBRTC; }
  public:
    bool Listen(int signaling_port, const qrpc_endpoint_t &ep) {
      return base::webrtc::Listener::Listen(signaling_port, base::webrtc::Listener::Endpoint::From(ep));
    }
  private:
    Worker &worker_;
    qrpc_listen_conf_t config_;
    int port_index_;
  };

    // webrtc::ClientConnection
  class ClientConnection : public base::webrtc::Client::Connection {
  public:
    ClientConnection(ConnectionFactory &cf, DtlsTransport::Role dtls_role, const qrpc_connect_conf_t &conf) :
      Connection(cf, dtls_role, base::webrtc::Client::TransportConfig::From(conf.transport), 
        [conf](const Stream::Config &c, base::Connection &conn) {
          auto &cc = dynamic_cast<ClientConnection &>(conn);
          auto he = qrpc_closure_call(conf.stream_router, c.label.c_str(), cc.ToHandle());
          if (he == nullptr) {
            logger::die({{"ev","no handler found"},{"label",c.label},{"ptr",base::str::dptr(&conn)}});
          }
          return std::shared_ptr<Stream>(qrpc::webrtc::NewStream(c, conn, *he));
        }), on_open_(conf.on_open), on_close_(conf.on_close), on_finalize_(conf.on_finalize) {}
    qrpc_conn_t ToHandle() { return { .p = this, .s = 0 }; }
    ~ClientConnection() override { qrpc_closure_call(on_finalize_, ToHandle()); }
    int OnConnect() override { return qrpc_closure_call(on_open_, ToHandle(), &ctx_); }
    qrpc_time_t OnShutdown() override { return qrpc_closure_call(on_close_, ToHandle(), &close_reason_->To(), &ctx_); }
  protected:
    void *ctx_;
    qrpc_on_client_conn_open_t on_open_;
    qrpc_on_client_conn_close_t on_close_;
    qrpc_on_client_conn_finalize_t on_finalize_;
  };

  // webrtc::Client
  class Client : public Loop, public ClientInterface {
  public:
    Client(const qrpc_clconf_t &config) : Loop(), resolver_(*this), config_(config), transport_(
      OpenOrDie(config.max_nfd, config.poll_timeout_ns),
      base::webrtc::Client::Config::From(
        resolver_.InitOrDie(AsyncResolver::Config::From(config.dns)), config.session_timeout, config.connect_timeout
      )) {}
    void Close(base::Connection &c) override { transport_.Close(c); }
    bool Connect(const qrpc_connect_conf_t &c) override {
      return transport_.Connect(
        base::webrtc::Client::Endpoint::From(c.ep),
        [c](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
          return new ClientConnection(cf, role, c);
        }
      );
    }
    void Poll() override { Loop::Poll(); }
    void Close() override { transport_.Fin(); }
  private:
    AsyncResolver resolver_;
    qrpc_clconf_t config_;
    base::webrtc::Client transport_;
  };
}
}