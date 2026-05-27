#include "qrpc/transports/webrtc.h"

namespace qrpc {
namespace webrtc {
  std::shared_ptr<base::Media> ServerConnection::media_factory(
    const std::string &path, base::Media::Direction direction
  ) {
    auto h = qrpc_closure_call(factory().to<webrtc::Listener>().config().media_router, path.c_str(), ToHandle());
    return factory().to<webrtc::Listener>().allocator().NewMedia(path, direction, *this, *h);
  }
  std::shared_ptr<base::Media> ClientConnection::media_factory(
    const std::string &path, base::Media::Direction direction
  ) {
    auto h = qrpc_closure_call(config_.media_router, path.c_str(), ToHandle());
    return factory().to<webrtc::Client>().allocator().NewMedia(path, direction, *this, *h);
  }
}
}
