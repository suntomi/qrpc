#pragma once
#include <stdlib.h>
#include <type_traits>

namespace base {
namespace conv {
static void *ref_decl = nullptr;

//specialized converters
template <
  class T,
  typename std::enable_if<std::is_integral<T>::value >::type*& = ref_decl
>
static bool Conv(const char *src, T *dst) {
  char *buf;
  uint64_t tmp = strtoll(src, &buf, 10);
  if ((*buf) == 0) {
    *dst = (T)tmp;
    return true;
  }
  return false;  
}
template <class T>
static bool Conv(const std::string &src, T *dst) {
  return Conv<T>(src.c_str(), dst);
}
} //namespace conv
} //namespace base
