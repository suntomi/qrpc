#pragma once

#include "base/defs.h"
#include <sstream>
#define  _XOPEN_SOURCE_EXTENDED 1
#include <strings.h>

namespace base {
namespace str {
  inline std::string HexDump(const uint8_t *p, size_t len) {
    constexpr char hex[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < len; i++) {
      auto b = p[i];
      s += hex[b >> 4];
      s += hex[b & 0xF];
    }
    return s;
  }
  static char *dptr(void *p) {
    static thread_local char buff[32];
    snprintf(buff, sizeof(buff), "%p", p);
    return buff;
  }
  static std::string Join(const std::vector<std::string>& elements, const std::string& delimiter) {
    std::ostringstream os;
    for (auto it = elements.begin(); it != elements.end(); ++it) {
        if (it != elements.begin()) {
            os << delimiter;
        }
        os << *it;
    }
    return os.str();
  }
  static std::vector<std::string> Split(const std::string &s, const std::string &delim) {
    std::vector<std::string> v;
    size_t start = 0;
    size_t end = s.find(delim);
    while (end != std::string::npos) {
      v.push_back(s.substr(start, end - start));
      start = end + delim.length();
      end = s.find(delim, start);
    }
    v.push_back(s.substr(start, end));
    return v;
  }
  static size_t Vprintf(char *buff, qrpc_size_t sz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buff, sz, fmt, ap);
    va_end(ap);
    return r;
  }
  template<class... Args>
  static inline std::string Format(size_t bufsz, const char *fmt, const Args... args) {
    char buff[bufsz];
    Vprintf(buff, sizeof(buff), fmt, args...);
    return std::string(buff);
  }
  template<class... Args>
  static inline std::string Format(const char *fmt, const Args... args) {
    return Format(1024, fmt, args...);
  }
  inline int CmpNocase(const std::string &a, const std::string &b, size_t n) {
    return strncasecmp(a.c_str(), b.c_str(), n);
  }
  inline void *Dup(const char *str, size_t n = 1024) {
    return strndup(str, n);
  }
}
}