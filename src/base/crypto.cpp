#include "base/crypto.h"

namespace base {
  namespace random {
    thread_local std::mt19937 engine(std::random_device{}());
    std::mt19937 &prng() { return engine; }
  }
}