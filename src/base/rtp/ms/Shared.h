// replace RTC::Shared
#pragma once

#include "ChannelMessageRegistrator.hpp"
#include "base/rtp/ms/Channel/ChannelNotifier.hpp"
#include "base/rtp/ms/PayloadChannel/PayloadChannelNotifier.hpp"

namespace base {
namespace ms {
  class VoidChannelSocket : public Channel::ChannelSocket {
  public:
    VoidChannelSocket() : Channel::ChannelSocket(
      ChannelReadFn, nullptr, ChannelWriteFn, nullptr
    ) {}
    static void ChannelReadFreeFnImpl(uint8_t*, uint32_t, size_t) {}
    static ChannelReadFreeFn ChannelReadFn(
      uint8_t** /* message */,
      uint32_t* /* messageLen */,
      size_t* /* messageCtx */,
      // This is `uv_async_t` handle that can be called later with `uv_async_send()`
      // when there is more data to read.
      const void* /* handle */,
      ChannelReadCtx /* ctx */
    ) {
      return ChannelReadFreeFnImpl;
    }
    static void ChannelWriteFn(
      const uint8_t* /* message */, uint32_t /* messageLen */, ChannelWriteCtx /* ctx */
    ) {}
  };
  class VoidPayloadChannelSocket : public PayloadChannel::PayloadChannelSocket {
  public:
    VoidPayloadChannelSocket() : PayloadChannel::PayloadChannelSocket(
      PayloadChannelReadFn, nullptr, PayloadChannelWriteFn, nullptr
    ) {}
    static void PayloadChannelReadFreeFnImpl(uint8_t*, uint32_t, size_t) {}
    static PayloadChannelReadFreeFn PayloadChannelReadFn(
      uint8_t** /* message */,
      uint32_t* /* messageLen */,
      size_t* /* messageCtx */,
      uint8_t** /* payload */,
      uint32_t* /* payloadLen */,
      size_t* /* payloadCapacity */,
      // This is `uv_async_t` handle that can be called later with `uv_async_send()`
      // when there is more data to read.
      const void* /* handle */,
      PayloadChannelReadCtx /* ctx */
    ) {
      return PayloadChannelReadFreeFnImpl;
    }
    static void PayloadChannelWriteFn(
      const uint8_t* /* message */,
      uint32_t /* messageLen */,
      const uint8_t* /* payload */,
      uint32_t /* payloadLen */,
      ChannelWriteCtx /* ctx */) {}
  };
	class Shared
	{
	public:
		explicit Shared(): 
      channelMessageRegistrator(new ChannelMessageRegistrator()),
      channelNotifier(new Channel::ChannelNotifier(GetChannelSocket())),
      payloadChannelNotifier(new PayloadChannel::PayloadChannelNotifier(GetPayloadChannelSocket())) {}
		~Shared() {
      delete this->channelMessageRegistrator;
		  delete this->channelNotifier;
		  delete this->payloadChannelNotifier;
    }

	public:
		ChannelMessageRegistrator* channelMessageRegistrator{ nullptr };
		Channel::ChannelNotifier* channelNotifier{ nullptr };
		PayloadChannel::PayloadChannelNotifier* payloadChannelNotifier{ nullptr };

  protected:
    std::unique_ptr<VoidChannelSocket> cs_;
    std::unique_ptr<VoidPayloadChannelSocket> pcs_;
    VoidChannelSocket *GetChannelSocket() {
      cs_ = std::make_unique<VoidChannelSocket>();
      return cs_.get();
    }
    VoidPayloadChannelSocket *GetPayloadChannelSocket() {
      pcs_ = std::make_unique<VoidPayloadChannelSocket>();
      return pcs_.get();
    }
	};
} // namespace msrtp
} // namespace base