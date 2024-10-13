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
  std::shared_ptr<Consumer> ConsumerFactory::Create(
    const Producer &consumed_producer, const std::string &label, Parameters::MediaKind kind,
    RTC::RtpParameters::Type type, const RTC::RtpParameters &p, bool paused
  ) {
    // generate rtp parameter from this handler_'s capabality (of corresponding producer) and consumed_producer's encodings
    // now rtx is always enabled
    // TODO: support pipe consumer & disable rtx?
    auto m = handler_.FindFrom(label);
		::flatbuffers::FlatBufferBuilder fbb;
    auto encodings = consumed_producer.PackConsumableEncodings(fbb);
    auto id = ProducerFactory::GenerateId(handler_.rtp_id(), label, kind);
    auto &producer_id = consumed_producer.id;
    fbb.Finish(FBS::Transport::CreateConsumeRequestDirect(
      fbb, id.c_str(), producer_id.c_str(), static_cast<FBS::RtpParameters::MediaKind>(kind),
      p.FillBuffer(fbb), RTC::RtpParameters::TypeToFbs(type), &encodings, paused
    ));
		auto data = flatbuffers::GetRoot<FBS::Transport::ConsumeRequest>(fbb.GetBufferPointer());
		try {
      std::shared_ptr<Consumer> c;
      switch (type) {
        case RTC::RtpParameters::Type::SIMPLE:
          c = std::make_shared<Wrap<RTC::SimpleConsumer>>(&handler_.shared(), id, producer_id, &handler_, data, m);
        case RTC::RtpParameters::Type::SIMULCAST:
          c = std::make_shared<Wrap<RTC::SimulcastConsumer>>(&handler_.shared(), id, producer_id, &handler_, data, m);
        case RTC::RtpParameters::Type::PIPE:
          c = std::make_shared<Wrap<RTC::PipeConsumer>>(&handler_.shared(), id, producer_id, &handler_, data, m);
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