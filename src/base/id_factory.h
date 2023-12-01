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
  IdFactory(NUMBER init = 0) : seed_(init), limit_(kLimit), incr_(1), init_(init) {}
  void set_limit(NUMBER limit) { limit_ = limit; }
  void set_incr(NUMBER incr) { incr_ = incr; }
  void set_init(NUMBER init) { init_ = init; seed_ = init; }

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
