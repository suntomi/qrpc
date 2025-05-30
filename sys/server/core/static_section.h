#pragma once

namespace qrpc {
//statically allocated section which is related with object which is allocated with
//AllocatorWithBSS. this memory block keep on being alive even if related object freed.
class StaticSection {
  std::mutex mutex_;
 public:
  StaticSection() : mutex_() {}
  ~NqStaticSection() {}
  std::mutex &mutex() { return mutex_; }
};  
}