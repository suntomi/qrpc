#pragma once

#include "base/rtp/ms/Channel/ChannelNotifier.hpp"
#include "base/rtp/ms/PayloadChannel/PayloadChannelNotifier.hpp"

namespace base {
namespace ms {
	class Shared
	{
	public:
		explicit Shared(): 
      cs_(Writer),
      pcs_(Writer),
      channelMessageRegistrator(new ChannelMessageRegistrator()),
      channelNotifier(new Channel::ChannelNotifier(&cs_)),
      payloadChannelNotifier(new PayloadChannel::PayloadChannelNotifier(&pcs_)) {}
		~Shared() {
      delete this->channelMessageRegistrator;
		  delete this->channelNotifier;
		  delete this->payloadChannelNotifier;
    }

  public:
    static void Writer(const json &j, bool success) {
      json mj = j;
      if (success) {
        mj["ev"] = "ms rtp emit";
        logger::info(mj);
      } else {
        mj["ev"] = "ms rtp error";
        logger::error(mj);
      }
    }

	public:
    Channel::ChannelSocket cs_;
    PayloadChannel::PayloadChannelSocket pcs_;
		ChannelMessageRegistrator* channelMessageRegistrator{ nullptr };
		Channel::ChannelNotifier* channelNotifier{ nullptr };
		PayloadChannel::PayloadChannelNotifier* payloadChannelNotifier{ nullptr };
};
} // namespace ms
} // namespace base