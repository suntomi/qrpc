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
	std::string ConsumerFactory::GenerateId(const std::string &id, const std::string &label, Parameters::MediaKind kind) { 
		return "/c/" + id + "/" + label + "/" + Parameters::FromMediaKind(kind);
	}
  std::shared_ptr<Consumer> ConsumerFactory::Create(
    const Handler &peer, const Producer &local_producer, const std::string &label, Parameters::MediaKind kind,
    bool paused, bool pipe, std::vector<uint32_t> &generated_ssrcs
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
    RTC::RtpParameters consumer_params;
    // generate rtp parameter from this handler_'s capabality (of corresponding producer) and consumed_producer's encodings
		if (!local_producer.consumer_params(consumed_producer->params(), consumer_params)) {
			QRPC_LOGJ(error, {{"ev","fail to generate cosuming params"}});
			ASSERT(false);
			return nullptr;
		}
    auto m = handler_.FindFrom(label);
		::flatbuffers::FlatBufferBuilder fbb;
    auto encodings = consumed_producer->params().PackConsumableEncodings(fbb, generated_ssrcs);
    auto id = GenerateId(peer.rtp_id(), label, kind);
    auto &producer_id = consumed_producer->id;
    fbb.Finish(FBS::Transport::CreateConsumeRequestDirect(
      fbb, id.c_str(), producer_id.c_str(), static_cast<FBS::RtpParameters::MediaKind>(kind),
      consumer_params.FillBuffer(fbb), RTC::RtpParameters::TypeToFbs(type), &encodings, 0, paused
    ));
		auto data = flatbuffers::GetRoot<FBS::Transport::ConsumeRequest>(fbb.GetBufferPointer());
		try {
      std::shared_ptr<Consumer> c;
      switch (type) {
        case RTC::RtpParameters::Type::SIMPLE:
          c = std::make_shared<Wrap<RTC::SimpleConsumer>>(&handler_.shared(), id, producer_id, &handler_, data, m);
          break;
        case RTC::RtpParameters::Type::SIMULCAST:
          c = std::make_shared<Wrap<RTC::SimulcastConsumer>>(&handler_.shared(), id, producer_id, &handler_, data, m);
          break;
        case RTC::RtpParameters::Type::PIPE:
          c = std::make_shared<Wrap<RTC::PipeConsumer>>(&handler_.shared(), id, producer_id, &handler_, data, m);
          break;
        default:
          QRPC_LOG(error, "invalid type: %d", type);
          return nullptr;
      }
      consumers_[id] = c;
      return c;
    } catch (std::exception &e) {
      QRPC_LOG(error, "failed to create consumer: %s", e.what());
      return nullptr;
    }
  }
}
}