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
    int OnConnect() override { return qrpc_closure_call(on_open_, ToHandle(), &ctx_); }
    qrpc_time_t OnShutdown() override { return qrpc_closure_call(on_close_, ToHandle(), ctx_); }
  }

  // NewStream
  static inline Stream *NewStream(
    Worker &w, const Stream::Config &c, base::Connection &conn, const HandlerEntry &he
  ) {
    Stream *s;
    Connection &wc = dynamic_cast<Connection &>(conn);
    switch (he.type) {
    case HandlerMap::DIRECTOR: {
      return (Stream *)qrpc_closure_call(he.director, nullptr, c.label.c_str(), wc.ToHandle());
    } break;
    case HandlerMap::STREAM: {
      if (qrpc_closure_is_empty(he.stream.stream_reader)) {
        return new CodedByteStream(wc, c, he.stream);
      } else {
        return new RawByteStream(wc, c, he.stream); 
      }
    } break;
    case HandlerMap::RPC: {
      return new RPCStream(wc, c, he.rpc, w.loop().alarm_processor());
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
  class ServerConnection : public base::webrtc::Connection {
  public:
    ServerConnection(ConnectionFactory &cf, DtlsTransport::Role dtls_role, const qrpc_svconf_t &config) :
      base::webrtc::Connection(cf, dtls_role) {}
    qrpc_conn_t ToHandle() { return { .p = this, .s = 0 }; }
    int OnConnect() override { return qrpc_closure_call(on_open_, ToHandle(), &ctx_); }
    qrpc_time_t OnShutdown() override { return qrpc_closure_call(on_close_, ToHandle(), ) }
  protected:
    void *ctx_;
    qrpc_on_server_conn_open_t on_open_;
    qrpc_on_server_conn_close_t on_close_;
  };

  // webrtc::Listener
  class Listener : public base::webrtc::Listener, BaseListener {
  public:
    Listener(Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config) : base::webrtc::Listener(
      w.loop(), ConfigFrom(addr, config.transport),
      // connection factory method
      [this](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
        return new Connection(cf, role);
      },
      // stream factory
      [this, &w](const Stream::Config &c, base::Connection &conn) {
        auto he = this->worker_.HandlerMapFor(this->port_index_).Find(c.label);
        return he != nullptr ? std::shared_ptr<Stream>(NewStream(w, c, conn, *he)) : nullptr;
      }
    ), worker_(w), handler_map_(HandlerMap::empty()), config_(config), addr_(addr), port_index_(port_index) {}
  public:
    static Listener *New(
      const Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config
    );
  private:
    Worker &worker_;
    HandlerMap &handler_map_;
    qrpc_svconf_t config_;
    qrpc_addr_t addr_;
    int port_index_;
  };

  // WebRTCClient
  class WebRTCClient : public base::webrtc::Client, Client {
    WebRTCClient(Worker &w, const qrpc_addr_t &addr, const qrpc_clconf_t &config) : base::webrtc::Client(
      w.loop(), ConfigFrom(addr, config.transport),
      // connection factory method
      [this](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
        return new WebRTCConnection(cf, role);
      },
      // stream factory
      [this, &w](const Stream::Config &c, base::Connection &conn) {
        auto he = this->worker_.HandlerMapFor(this->port_index_).Find(c.label);
        return he != nullptr ? std::shared_ptr<Stream>(NewStream(w, c, conn, *he)) : nullptr;
      }
    ), worker_(w), handler_map_(HandlerMap::empty()), config_(config), addr_(addr), port_index_(port_index) {}
  public:
    static WebRTCListener *New(
      const Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_clconf_t &config
    );
  private:
    Worker &worker_;
    HandlerMap &handler_map_;
    qrpc_svconf_t config_;
    qrpc_addr_t addr_;
    int port_index_;  };
}
}