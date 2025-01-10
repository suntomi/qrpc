#include "base/rtp/consumer.h"
#include "base/rtp/handler.h"
#include "base/string.h"

#include "RTC/PipeConsumer.hpp"
#include "RTC/SimpleConsumer.hpp"
#include "RTC/SimulcastConsumer.hpp"

namespace base {
namespace rtp {
  template <class C>
  class Wrap : public C {
  public:
    Wrap(RTC::Shared* s, 
      const std::string& id, const std::string& producer_id,
      RTC::Consumer::Listener *l, const FBS::Transport::ConsumeRequest* d,
      std::shared_ptr<Media> m
    ) : C(s, id, producer_id, l, d), media_(m) {}
    ~Wrap() override {}
    Handler &handler() { return *dynamic_cast<Handler *>(C::listener); }
  protected:
    std::shared_ptr<Media> media_;
  };
	std::string ConsumerFactory::GenerateId(const std::string &id, const std::string &path) { 
		return "/c/" + id + "/" + path;
	}
  Consumer *ConsumerFactory::Create(
    const Handler &peer, const MediaStreamConfig &config, bool pipe
  ) {
    const auto &path = config.media_path;
    const auto lpath = config.local_path();
    const auto kind = config.kind;
		auto consumed_producer = peer.FindProducerByPath(lpath);
		if (consumed_producer == nullptr) {
			QRPC_LOGJ(error, {{"ev","fail to find consumed producer"},{"path",path}});
			ASSERT(false);
			return nullptr;
		}
    auto type = pipe ?
      RTC::RtpParameters::Type::PIPE :
      (consumed_producer->params().encodings.size() > 1 ? RTC::RtpParameters::Type::SIMULCAST : RTC::RtpParameters::Type::SIMPLE);
    auto m = handler_.FindFrom(path, true);
		::flatbuffers::FlatBufferBuilder fbb;
    auto encodings = consumed_producer->params().PackConsumableEncodings(fbb);
    auto id = GenerateId(handler_.rtp_id(), path);
    auto &producer_id = consumed_producer->id;
    Handler::SetConsumerFactory([m](
			RTC::RtpParameters::Type type,
			RTC::Shared* shared,
			const std::string& id,
			const std::string& producer_id,
			RTC::Consumer::Listener* listener,
			const FBS::Transport::ConsumeRequest* data
    ) mutable -> RTC::Consumer * {
      switch (type) {
        case RTC::RtpParameters::Type::SIMPLE:
          return new Wrap<RTC::SimpleConsumer>(shared, id, producer_id, listener, data, m);
        case RTC::RtpParameters::Type::SIMULCAST:
          return new Wrap<RTC::SimulcastConsumer>(shared, id, producer_id, listener, data, m);
        case RTC::RtpParameters::Type::PIPE:
          return new Wrap<RTC::PipeConsumer>(shared, id, producer_id, listener, data, m);
        default:
          QRPC_LOG(error, "invalid type: %d", type);
          return nullptr;
      }
    });
		try {
      handler_.HandleRequest(fbb, FBS::Request::Method::TRANSPORT_CONSUME, FBS::Transport::CreateConsumeRequestDirect(
        fbb, id.c_str(), producer_id.c_str(), static_cast<FBS::RtpParameters::MediaKind>(kind),
        config.FillBuffer(fbb), RTC::RtpParameters::TypeToFbs(type), &encodings, 0, config.options.pause
      ));
      return handler_.FindConsumer(id);
    } catch (std::exception &e) {
      QRPC_LOG(error, "failed to create consumer: %s", e.what());
      return nullptr;
    }
  }
  Handler &ConsumerFactory::HandlerFrom(Consumer *c) {
    switch (c->GetType()) {
      case RTC::RtpParameters::Type::SIMPLE:
        return dynamic_cast<Wrap<RTC::SimpleConsumer>*>(c)->handler();
      case RTC::RtpParameters::Type::SIMULCAST:
        return dynamic_cast<Wrap<RTC::SimulcastConsumer>*>(c)->handler();
      case RTC::RtpParameters::Type::PIPE:
        return dynamic_cast<Wrap<RTC::PipeConsumer>*>(c)->handler();
    }
  }
  flatbuffers::Offset<FBS::Consumer::DumpResponse>
  ConsumerFactory::FillBuffer(Consumer *c, flatbuffers::FlatBufferBuilder& builder) {
    switch (c->GetType()) {
      case RTC::RtpParameters::Type::SIMPLE:
        return dynamic_cast<RTC::SimpleConsumer *>(c)->FillBuffer(builder);
      case RTC::RtpParameters::Type::SIMULCAST:
        return dynamic_cast<RTC::SimulcastConsumer *>(c)->FillBuffer(builder);
      case RTC::RtpParameters::Type::PIPE:
        return dynamic_cast<RTC::PipeConsumer *>(c)->FillBuffer(builder);
      default:
        QRPC_LOG(error, "unsupported type: %d", c->GetType());
        ASSERT(false);
        return flatbuffers::Offset<FBS::Consumer::DumpResponse>(0);
    }
  }
}
}