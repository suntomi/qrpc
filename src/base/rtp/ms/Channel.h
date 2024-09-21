#pragma once

// prevent redefinition of ChannelSocket and PayloadChannelSocket
#define MS_CHANNEL_SOCKET_HPP
#define MS_PAYLOAD_CHANNEL_SOCKET_HPP
#define MS_LOGGER_HPP

#include <string>
#include <nlohmann/json.hpp>

#include "base/logger.h"
#include "base/webrtc/mpatch.h"

#include "base/rtp/ms/Channel/ChannelRequest.hpp"
#include "base/rtp/ms/PayloadChannel/PayloadChannelRequest.hpp"
#include "base/rtp/ms/PayloadChannel/PayloadChannelNotification.hpp"

using json = nlohmann::json;

namespace base {
namespace ms {
  class ChannelBase {
  public:
    virtual void Write(const json &, bool) = 0;
    void Send(const json &j) { Write(j, true); }
    void Send(const std::string &s, const uint8_t *p, size_t pl) {
      try {
        auto j = json::parse(s);
        j["data"] = logger::hexdump(p, pl);
        Send(j);
      } catch (const std::exception &e) {
        Write({
          {"data", s},
          {"error", e.what()}
        }, false);
      }
    }
    void Send(const std::string &s) {
      try {
        Send(json::parse(s));
      } catch (const std::exception &e) {
        Write({
          {"data", s},
          {"error", e.what()}
        }, false);
      }
    }
  };
  namespace Channel {
    class ChannelSocket : public base::ms::ChannelBase {
    public:
      class RequestHandler {
      public:
        virtual ~RequestHandler() = default;
      public:
        virtual void HandleRequest(Channel::ChannelRequest* request) = 0;
      };
    public:
      typedef std::function<void(const json &, bool)> SendCallback;
      ChannelSocket(const SendCallback &&scb) : scb_(scb) {}
      void Write(const json &msg, bool success) override { scb_(msg, success); }
    protected:
      SendCallback scb_;
    };
  }
  namespace PayloadChannel {
    class PayloadChannelSocket : public base::ms::ChannelBase {
    public:
      class RequestHandler {
      public:
        virtual ~RequestHandler() = default;
      public:
        virtual void HandleRequest(PayloadChannel::PayloadChannelRequest* request) = 0;
      };
      class NotificationHandler {
      public:
        virtual ~NotificationHandler() = default;
      public:
        virtual void HandleNotification(PayloadChannel::PayloadChannelNotification* notification) = 0;
      };
    public:
      typedef std::function<void(const json &, bool)> SendCallback;
      PayloadChannelSocket(const SendCallback &&scb) : scb_(scb) {}
      void Write(const json &msg, bool success) override { scb_(msg, success); }
    protected:
      SendCallback scb_;
    };
  }
  class ChannelMessageRegistrator
  {
  public:
    explicit ChannelMessageRegistrator();
    ~ChannelMessageRegistrator();

  public:
    void FillJson(json& jsonObject);
    void RegisterHandler(
      const std::string& id,
      Channel::ChannelSocket::RequestHandler* channelRequestHandler,
      PayloadChannel::PayloadChannelSocket::RequestHandler* payloadChannelRequestHandler,
      PayloadChannel::PayloadChannelSocket::NotificationHandler* payloadChannelNotificationHandler);
    void UnregisterHandler(const std::string& id);
    Channel::ChannelSocket::RequestHandler* GetChannelRequestHandler(const std::string& id);
    PayloadChannel::PayloadChannelSocket::RequestHandler* GetPayloadChannelRequestHandler(
      const std::string& id);
    PayloadChannel::PayloadChannelSocket::NotificationHandler* GetPayloadChannelNotificationHandler(
      const std::string& id);

  private:
    absl::flat_hash_map<std::string, Channel::ChannelSocket::RequestHandler*> mapChannelRequestHandlers;
    absl::flat_hash_map<std::string, PayloadChannel::PayloadChannelSocket::RequestHandler*>
      mapPayloadChannelRequestHandlers;
    absl::flat_hash_map<std::string, PayloadChannel::PayloadChannelSocket::NotificationHandler*>
      mapPayloadChannelNotificationHandlers;
  };
} // namespace ms
} // namespace base

