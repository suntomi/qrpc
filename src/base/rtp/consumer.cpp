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
  protected:
    std::shared_ptr<Media> media_;
  };
	std::string ConsumerFactory::GenerateId(
    const std::string &id, const std::string &peer_id, const std::string &label, Parameters::MediaKind kind) { 
		return "/c/" + id + "/" + peer_id + "/" + label + "/" + Parameters::FromMediaKind(kind);
	}
  Consumer *ConsumerFactory::Create(
    const Handler &peer, const std::string &label, Parameters::MediaKind kind,
    const RTC::RtpParameters &consumer_params, bool paused, bool pipe
  ) {
		auto consumed_producer = peer.FindProducer(label, kind);
		if (consumed_producer == nullptr) {
			QRPC_LOGJ(error, {{"ev","fail to find consumed producer"},{"l",label},{"kind",kind}});
			ASSERT(false);
			return nullptr;
		}
    auto type = pipe ?
      RTC::RtpParameters::Type::PIPE :
      (consumed_producer->params().encodings.size() > 1 ? RTC::RtpParameters::Type::SIMULCAST : RTC::RtpParameters::Type::SIMPLE);
    auto m = handler_.FindFrom(label);
		::flatbuffers::FlatBufferBuilder fbb;
    auto encodings = consumed_producer->params().PackConsumableEncodings(fbb);
    auto id = GenerateId(handler_.rtp_id(), peer.rtp_id(), label, kind);
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
        consumer_params.FillBuffer(fbb), RTC::RtpParameters::TypeToFbs(type), &encodings, 0, paused
      ));
      return handler_.FindConsumer(id);
    } catch (std::exception &e) {
      QRPC_LOG(error, "failed to create consumer: %s", e.what());
      return nullptr;
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