#include <tuple>

#include "RTC/DtlsTransport.hpp"

namespace base {
namespace webrtc {
  typedef std::tuple<bool, std::string, int, std::string, std::string, uint64_t, RTC::DtlsTransport::Fingerprint> Candidate;
} // namespace webrtc
} // namespace base