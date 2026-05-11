#include "base/media.h"
#include "base/conn.h"
#include "base/rtp/handler.h"

#define FIND_AND_OP(__path, ...) { \
  if (direction_ == Direction::SEND) { \
    auto p = connection().rtp().FindProducerByPath(__path); \
    if (p == nullptr) { \
      DIE("cannot find producer at " + __path); \
    } \
    auto &o = *p; \
    __VA_ARGS__; \
  } else { \
    ASSERT(direction_ == Direction::RECV); \
    auto c = connection().rtp().FindConsumerByPath(__path); \
    if (c == nullptr) { \
      DIE("cannot find consumer at " + __path); \
    } \
    auto &o = *c; \
    __VA_ARGS__; \
  } \
}

namespace base {
  qrpc_media_kind_t Media::kind() const {
    FIND_AND_OP(path_, switch (o.GetKind()) {
      case RTC::Media::Kind::AUDIO:
        return QRPC_MEDIA_KIND_AUDIO;
      case RTC::Media::Kind::VIDEO:
        return QRPC_MEDIA_KIND_VIDEO;
      default:
        DIE("unknown media kind");
    });
  }
}
