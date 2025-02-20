#include "base/rtp/consumer.h"
#include "base/rtp/handler.h"
#include "base/string.h"

#include "RTC/PipeConsumer.hpp"
#include "RTC/SimpleConsumer.hpp"
#include "RTC/SimulcastConsumer.hpp"

namespace base {
namespace rtp {
  json ConsumerStatus::ToJson() const {
    json pausedReasons;
    if (paused) {
      pausedReasons.push_back("local_op");
    }
    if (producerPaused) {
      pausedReasons.push_back("remote_op");
    }
    return {
      {"pausedReasons", pausedReasons},
    };
  }
  template <class C>
  class Wrap : public C {
  public:
    Wrap(RTC::Shared* s, 
      const std::string& id, const std::string& producer_id,
      RTC::Consumer::Listener *l, const FBS::Transport::ConsumeRequest* d,
      const MediaStreamConfig &config, std::shared_ptr<Media> m
    ) : C(s, id, producer_id, l, d), media_path_(config.media_path), media_(m) {}
    ~Wrap() override {}
    Handler &handler() { return *dynamic_cast<Handler *>(C::listener); }
    ConsumerStatus status() const {
      return ConsumerStatus{.paused = C::IsPaused(), .producerPaused = C::IsProducerPaused()};
    }
    void OnProducerManuallyClosed() {
      for (auto &c : handler().listener().media_stream_configs()) {
        if (c.media_path == media_path_) {
          c.close();
          return;
        }
      }
      ASSERT(false);
    }
  protected:
    const std::string media_path_;
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
			QRPC_LOGJ(error, {{"ev","fail to find consumed producer"},{"path",lpath},{"fpath",ProducerFactory::GenerateId(peer.rtp_id(), path)}});
			return nullptr; // on reconnection but does not prepared producer yet, it is possible
		}
    auto type = pipe ?
      RTC::RtpParameters::Type::PIPE :
      (consumed_producer->params().encodings.size() > 1 ? RTC::RtpParameters::Type::SIMULCAST : RTC::RtpParameters::Type::SIMPLE);
    auto m = handler_.FindFrom(path, true);
		::flatbuffers::FlatBufferBuilder fbb;
    auto encodings = consumed_producer->params().PackConsumableEncodings(fbb);
    auto id = GenerateId(handler_.rtp_id(), path);
    auto &producer_id = consumed_producer->id;
    Handler::SetConsumerFactory([config, m](
			RTC::RtpParameters::Type type,
			RTC::Shared* shared,
			const std::string& id,
			const std::string& producer_id,
			RTC::Consumer::Listener* listener,
			const FBS::Transport::ConsumeRequest* data
    ) mutable -> RTC::Consumer * {
      switch (type) {
        case RTC::RtpParameters::Type::SIMPLE:
          return new Wrap<RTC::SimpleConsumer>(shared, id, producer_id, listener, data, config, m);
        case RTC::RtpParameters::Type::SIMULCAST:
          return new Wrap<RTC::SimulcastConsumer>(shared, id, producer_id, listener, data, config, m);
        case RTC::RtpParameters::Type::PIPE:
          return new Wrap<RTC::PipeConsumer>(shared, id, producer_id, listener, data, config, m);
        default:
          QRPC_LOG(error, "invalid type: %d", type);
          return nullptr;
      }
    });
		try {
      handler_.HandleRequest(fbb, FBS::Request::Method::TRANSPORT_CONSUME, FBS::Transport::CreateConsumeRequestDirect(
        fbb, id.c_str(), producer_id.c_str(), static_cast<FBS::RtpParameters::MediaKind>(kind),
        config.FillBuffer(fbb), RTC::RtpParameters::TypeToFbs(type), &encodings, config.options.pause
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
      default:
        logger::die({{"ev","unsupported consumer type:"},{"type",c->GetType()}});
    }
  }
  ConsumerStatus ConsumerFactory::StatusFrom(Consumer *c) {
    switch (c->GetType()) {
      case RTC::RtpParameters::Type::SIMPLE:
        return dynamic_cast<Wrap<RTC::SimpleConsumer>*>(c)->status();
      case RTC::RtpParameters::Type::SIMULCAST:
        return dynamic_cast<Wrap<RTC::SimulcastConsumer>*>(c)->status();
      case RTC::RtpParameters::Type::PIPE:
        return dynamic_cast<Wrap<RTC::PipeConsumer>*>(c)->status();
      default:
        logger::die({{"ev","unsupported consumer type:"},{"type",c->GetType()}});
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
        logger::die({{"ev","unsupported consumer type:"},{"type",c->GetType()}});
    }
  }
  void ConsumerFactory::OnProducerManuallyClosed(Consumer *c) {
    switch (c->GetType()) {
      case RTC::RtpParameters::Type::SIMPLE:
        dynamic_cast<Wrap<RTC::SimpleConsumer>*>(c)->OnProducerManuallyClosed();
        break;
      case RTC::RtpParameters::Type::SIMULCAST:
        dynamic_cast<Wrap<RTC::SimulcastConsumer>*>(c)->OnProducerManuallyClosed();
        break;
      case RTC::RtpParameters::Type::PIPE:
        dynamic_cast<Wrap<RTC::PipeConsumer>*>(c)->OnProducerManuallyClosed();
        break;
      default:
        logger::die({{"ev","unsupported consumer type:"},{"type",c->GetType()}});
    }
  }
}
}