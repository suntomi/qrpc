#pragma once

#include "defs.h"
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
  static size_t Vprintf(char *buff, qrpc_size_t sz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buff, sz, fmt, ap);
    va_end(ap);
    return r;
  }
  inline int CmpNocase(const std::string &a, const std::string &b, size_t n) {
    return strncasecmp(a.c_str(), b.c_str(), n);
  }
  inline void *Dup(const char *str, size_t n = 1024) {
    return strndup(str, n);
  }
}
}