#pragma once

#include "base/webrtc.h"

#include "qrpc/base.h"
#include "qrpc/client.h"
#include "qrpc/handler_map.h"
#include "qrpc/listener.h"
#include "qrpc/server.h"
#include "qrpc/stream.h"

namespace qrpc {
  using ConnectionFactory = base::webrtc::ConnectionFactory;
  using DtlsTransport = base::webrtc::DtlsTransport;
  class WebRTCConnection : public base::webrtc::Connection {
  public:
      WebRTCConnection(ConnectionFactory &cf, DtlsTransport::Role dtls_role) : base::webrtc::Connection(cf, dtls_role) {}
      qrpc_conn_t ToHandle() { return { .p = this, .s = 0 }; }
  };
  class WebRTCListener : public base::webrtc::Listener, Listener {
  public:
    WebRTCListener(Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config) : base::webrtc::Listener(
      w.loop(), ConfigFrom(addr, config),
      // connection factory method
      [this](ConnectionFactory &cf, base::webrtc::DtlsTransport::Role role) {
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
      const Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config
    );
    static base::webrtc::Listener::Config ConfigFrom(
     const qrpc_addr_t &addr, const qrpc_svconf_t &config
    ) {
      return base::webrtc::Listener::Config {
        .ip = addr.host,
      };
    }
    static Stream *NewStream(
      Worker &w, const Stream::Config &c, base::Connection &conn, const HandlerEntry &he
    ) {
      Stream *s;
      WebRTCConnection &wc = dynamic_cast<WebRTCConnection &>(conn);
      switch (he.type) {
      case HandlerMap::FACTORY: {
        return (Stream *)qrpc_closure_call(he.factory, wc.ToHandle());
      } break;
      case HandlerMap::STREAM: {
        if (qrpc_closure_is_empty(he.stream.stream_reader)) {
          return new CodedByteStream(wc, c, he.stream);
        } else {
          return new RawByteStream(wc, c, he.stream); 
        }
      } break;
      case HandlerMap::RPC: {
        return new RPCStream(wc, c, he.rpc, w.timer());
      } break;
      default:
        ASSERT(false);
        return nullptr;
      }
    }
  private:
    Worker &worker_;
    HandlerMap &handler_map_;
    qrpc_svconf_t config_;
    qrpc_addr_t addr_;
    int port_index_;
  };
}