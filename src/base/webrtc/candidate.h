#include <tuple>

#include "base/webrtc/dtls.h"

namespace base {
namespace webrtc {
  typedef std::tuple<bool, std::string, int, std::string, std::string, uint64_t, DtlsTransport::Fingerprint> Candidate;
} // namespace webrtc
} // namespace base