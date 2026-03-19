#pragma once
#include <stdlib.h>
#include <type_traits>

namespace base {
namespace type {
  template <class To, class From>
  To &cast_or_die(From &f) {
    auto t = dynamic_cast<To *>(&f);
    if (t == nullptr) { base::logger::die({{"ev","cast to incompatible type"},{"to",typeid(To).name()}}); }
    return *t;
  }
} //namespace type
} //namespace base
