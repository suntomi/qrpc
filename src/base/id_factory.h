#pragma once

#include "base/defs.h"

namespace base {
template <typename NUMBER>
class IdFactory {
  atomic<NUMBER> seed_;
  NUMBER limit_, incr_, init_;
  //0xFFFFFF....
  static const NUMBER kLimit = 
    (((NUMBER)0x80) << (8 * (sizeof(NUMBER) - 1))) + 
    ((((NUMBER)0x80) << (8 * (sizeof(NUMBER) - 1))) - 1);
public:
  IdFactory(NUMBER init = 1, NUMBER incr = 1, NUMBER limit = kLimit) {
    configure(init, incr, limit);
  }
  void configure(NUMBER init, NUMBER incr, NUMBER limit = kLimit) {
    limit_ = limit;
    incr_ = incr;
    init_ = init;
    // New() adds incr_ to seed_ before returning it, so we need to subtract
    // to return init for first return value of New()
    auto true_init = init - incr;
    seed_ = true_init;
  }
  NUMBER New() {
    while (true) {
      NUMBER expect = seed_;
      NUMBER desired = expect + incr_;
      if (desired < seed_) { //round
        desired = init_;
      }
      if (atomic_compare_exchange_weak(&seed_, &expect, desired)) {
        return desired;
      }
  	}
    ASSERT(false);
    return 0;		
  }
};
//TODO(iyatomi): provide true NoAtomic version
template <class T>
using IdFactoryNoAtomic = IdFactory<T>;
}
