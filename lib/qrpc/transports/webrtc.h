#pragma once

#include "base/webrtc.h"

#include "qrpc/base.h"
#include "qrpc/client.h"
#include "qrpc/handler_map.h"
#include "qrpc/listener.h"
#include "qrpc/server.h"
#include "qrpc/stream.h"

namespace qrpc {
using BaseListener = Listener;
using BaseClient = Client;
namespace webrtc {
  using ConnectionFactory = base::webrtc::ConnectionFactory;
  using DtlsTransport = RTC::DtlsTransport;

  class Connection : public base::webrtc::Connection {
  public:
    Connection(ConnectionFactory &cf, DtlsTransport::Role dtls_role) :
      base::webrtc::Connection(cf, dtls_role) {}
    qrpc_conn_t ToHandle() { return { .p = this, .s = 0 }; }
  };

  // NewStream
  static inline Stream *NewStream(
    const Stream::Config &c, base::Connection &conn, const HandlerEntry &he
  ) {
    Stream *s;
    Connection &wc = dynamic_cast<Connection &>(conn);
    switch (he.type) {
    case HandlerType::STREAM: {
      if (qrpc_closure_is_empty(he.stream.stream_reader)) {
        return new CodedByteStream(wc, c, he.stream);
      } else {
        return new RawByteStream(wc, c, he.stream); 
      }
    } break;
    case HandlerType::RPC: {
      return new RPCStream(wc, c, he.rpc, conn.alarm_processor());
    } break;
    default:
      ASSERT(false);
      return nullptr;
    }
  }
  // ConfigFrom
  static inline base::webrtc::ConnectionFactory::Config ConfigFrom(
    const qrpc_addr_t &addr, const qrpc_transport_config_t &config
  ) {
    return base::webrtc::Listener::Config {
      .ip = addr.host,
    };
  }

  // webrtc::ServerConnection
  class ServerConnection : public Connection {
  public:
    ServerConnection(ConnectionFactory &cf, DtlsTransport::Role dtls_role, const qrpc_svconf_t &config) :
      Connection(cf, dtls_role) {}
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
  class Listener : public base::webrtc::Listener, public BaseListener {
  public:
    Listener(Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config) :
      base::webrtc::Listener(w.loop(), ConfigFrom(addr, config.transport),
        // connection factory method
        [this](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
          return new ServerConnection(cf, role, config_);
        },
        // stream factory
        [this](const Stream::Config &c, base::Connection &conn) {
          auto &wc = dynamic_cast<qrpc::webrtc::Connection &>(conn);
          auto he = this->worker_.HandlerMapFor(this->port_index_).Find(c.label, wc.ToHandle());
          return he != nullptr ? std::shared_ptr<Stream>(NewStream(c, conn, *he)) : nullptr;
        }
      ), worker_(w), handler_map_(HandlerMap::empty()), config_(config), addr_(addr), port_index_(port_index) {}
    HandlerMap &handler_map() override { return handler_map_; }
  private:
    Worker &worker_;
    HandlerMap &handler_map_;
    qrpc_svconf_t config_;
    qrpc_addr_t addr_;
    int port_index_;
  };

    // webrtc::ClientConnection
  class ClientConnection : public Connection {
  public:
    ClientConnection(ConnectionFactory &cf, DtlsTransport::Role dtls_role, const qrpc_clconf_t &config) :
      Connection(cf, dtls_role) {}
    int OnConnect() override { return qrpc_closure_call(on_open_, ToHandle(), &ctx_); }
    qrpc_time_t OnShutdown() override { return qrpc_closure_call(on_close_, ToHandle(), &close_reason_->To(), &ctx_); }
  protected:
    void *ctx_;
    qrpc_on_client_conn_open_t on_open_;
    qrpc_on_client_conn_close_t on_close_;
  };

  // webrtc::Client
  class Client : public Loop, public BaseClient {
  public:
    Client(const qrpc_clconf_t &config) : Loop(), resolver_(*this), config_(config), transport_(
      OpenOrDie(config.max_nfd, config.poll_timeout_ns),
      base::webrtc::Client::Config::From(config.transport, resolver_.InitOrDie(
        AsyncResolver::Config::From(config.dns)
      )),
      // connection factory method
      [this](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {  
        return new ClientConnection(cf, role, config_);
      },
      // stream factory
      [this](const Stream::Config &c, base::Connection &conn) {
        auto &wc = dynamic_cast<qrpc::webrtc::Connection &>(conn);
        HandlerEntry he; // TODO: how to get value for it?
        ASSERT(false);
        return std::shared_ptr<Stream>(NewStream(c, conn, he));
      }
    ) {}
    void Close(base::Connection &c) override { transport_.Close(c); }
    bool Connect(const qrpc_addr_t &addr, const qrpc_connect_conf_t &config) override {
      return transport_.Connect(addr.host, addr.port);
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