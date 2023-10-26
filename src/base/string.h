#pragma once

#include "base/defs.h"
#define  _XOPEN_SOURCE_EXTENDED 1
#include <strings.h>

namespace base {
namespace str {
  inline std::string HexDump(uint8_t *p, size_t len) {
    constexpr char hex[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < len; i++) {
      auto b = p[i];
      s += hex[b >> 4];
      s += hex[b & 0xF];
    }
    return s;
  }
  inline int cmp_nocase(const std::string &a, const std::string &b) {
    return strcasecmp(a.c_str(), b.c_str());
  }
}