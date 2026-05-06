#include "qrpc/transports/webrtc.h"

namespace qrpc {
namespace webrtc {
  std::shared_ptr<base::Media> ServerConnection::media_factory(const std::string &path) {
    auto h = qrpc_closure_call(factory().to<webrtc::Listener>().config().media_router, path.c_str(), ToHandle());
    return std::make_shared<Media>(path, *this, *h);
  }
  std::shared_ptr<base::Media> ClientConnection::media_factory(const std::string &path) {
    auto h = qrpc_closure_call(config_.media_router, path.c_str(), ToHandle());
    return std::make_shared<Media>(path, *this, *h);
  }
}
}
