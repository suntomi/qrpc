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
    const Producer &producer, const std::string &label, Parameters::MediaKind kind,
    RTC::RtpParameters::Type type, const Parameters &p
  ) {
    auto m = handler_.FindFrom(p);
		::flatbuffers::FlatBufferBuilder fbb;
    auto encodings = producer.PackConsumableEncodings(fbb);
    auto id = ProducerFactory::GenerateId(handler_.rtp_id(), label, kind);
    fbb.Finish(FBS::Transport::CreateConsumeRequestDirect(
      fbb, id.c_str(), producer.id.c_str(), static_cast<FBS::RtpParameters::MediaKind>(p.kind),
      p.FillBuffer(fbb), RTC::RtpParameters::TypeToFbs(type), &encodings
    ));
		auto data = flatbuffers::GetRoot<FBS::Transport::ConsumeRequest>(fbb.GetBufferPointer());
		try {
      switch (type) {
        case RTC::RtpParameters::Type::SIMPLE:
          return std::make_shared<Wrap<RTC::SimpleConsumer>>(&handler_.shared(), id, producer.id, &handler_, data, m);
        case RTC::RtpParameters::Type::SIMULCAST:
          return std::make_shared<Wrap<RTC::SimulcastConsumer>>(&handler_.shared(), id, producer.id, &handler_, data, m);
        case RTC::RtpParameters::Type::PIPE:
          return std::make_shared<Wrap<RTC::PipeConsumer>>(&handler_.shared(), id, producer.id, &handler_, data, m);
        default:
          QRPC_LOG(error, "invalid type: %d", type);
          return nullptr;
      }
    } catch (std::exception &e) {
      QRPC_LOG(error, "failed to create consumer: %s", e.what());
      return nullptr;
    }
  }
}
}