#ifndef MS_CHANNEL_NOTIFIER_HPP
#define MS_CHANNEL_NOTIFIER_HPP

#include "common.hpp"
#include "base/rtp/ms/Channel/ChannelSocket.hpp"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

namespace base {
namespace ms {
namespace Channel
{
	class ChannelNotifier
	{
	public:
		explicit ChannelNotifier(Channel::ChannelSocket* channel);

	public:
		void Emit(uint64_t targetId, const char* event);
		void Emit(const std::string& targetId, const char* event);
		void Emit(const std::string& targetId, const char* event, json& data);
		void Emit(const std::string& targetId, const char* event, const std::string& data);

	private:
		// Passed by argument.
		Channel::ChannelSocket* channel{ nullptr };
	};
} // namespace Channel
} // namespace ms
} // namespace base

#endif