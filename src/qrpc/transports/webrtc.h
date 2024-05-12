#pragma once

#include "base/webrtc.h"

#include "qrpc/base.h"
#include "qrpc/client.h"
#include "qrpc/handler_map.h"
#include "qrpc/listener.h"
#include "qrpc/server.h"

namespace qrpc {
  class WebRTCListener : public base::webrtc::Listener, Listener {
  public:
    WebRTCListener(Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config) : base::webrtc::Listener(
      w.loop(), WebRTCListener::ConfigFrom(addr, config), 
      [this](ConnectionFactory &cf, base::webrtc::DtlsTransport::Role role) {
        return new base::webrtc::Connection(cf, role, [this](){}, [this](){});
      }
    ), worker_(w), handler_map_(HandlerMap::empty()), config_(config), addr_(addr), port_index_(port_index) {}
  public:
    static std::unique_ptr<WebRTCListener> New(
      const Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config
    );
    static base::webrtc::Listener::Config ConfigFrom(
     const qrpc_addr_t &addr, const qrpc_svconf_t &config
    ) {
      return base::webrtc::Listener::Config {
        .ip = addr.host,
      };
    }
  private:
    Worker &worker_;
    HandlerMap &handler_map_;
    qrpc_svconf_t config_;
    qrpc_addr_t addr_;
    int port_index_;
  };
}