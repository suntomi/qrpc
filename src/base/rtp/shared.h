// replace RTC::Shared
#pragma once

#include "RTC/Shared.hpp"
#include "ChannelMessageRegistrator.hpp"
#include "Channel/ChannelNotifier.hpp"
#include "PayloadChannel/PayloadChannelNotifier.hpp"

namespace base {
namespace rtp {
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
	class Shared : public RTC::Shared
	{
	public:
    explicit Shared() : RTC::Shared(
      new ChannelMessageRegistrator(),
      new Channel::ChannelNotifier(new VoidChannelSocket()),
      new PayloadChannel::PayloadChannelNotifier(new VoidPayloadChannelSocket())
    ) {}
		~Shared();
	};
} // namespace rtp
} // namespace base