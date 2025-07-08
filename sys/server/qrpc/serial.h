#pragma once

#include <map>
#include <mutex>
#include <algorithm>

#include <inttypes.h>

#include "qrpc.h"
#include "base/id_factory.h"

namespace qrpc {
/*
conn serial: 64 bit
  [thread_id: 16 bit][serial 48bit]

stream serial: 128 bit
  [thread_id: 16 bit][serial 48bit]

alarm serial: 128 bit
  [thread_id: 16 bit][serial 48bit]
*/

class Serial : public qrpc_serial_t {
 public:
  static inline bool IsSame(const qrpc_serial_t &s1, const qrpc_serial_t &s2) {
    return s1.data[0] == s2.data[0];
  }
  static inline bool IsEmpty(const qrpc_serial_t &serial) {
    return serial.data[0] == 0;
  }
  static inline void Clear(qrpc_serial_t &serial) {
    serial.data[0] = 0;
  }
  static inline const std::string Dump(const qrpc_serial_t &serial) {
    char buff[256];
    auto sz = snprintf(buff, sizeof(buff), "%" PRIx64, serial.data[0]);
    return std::string(buff, sz);
  }
  static inline bool Compare(const qrpc_serial_t &s1, const qrpc_serial_t &s2) {
    return s1.data[0] < s2.data[0];    
  }
  struct Comparer {
    inline bool operator() (const qrpc_serial_t& lhs, const qrpc_serial_t& rhs) const {
      return Compare(lhs, rhs);
    }
  };
  static inline const Serial &New() {
    static Serial s;
    return s;
  }
 public:
  inline Serial() { Clear(); }
  inline Serial(qrpc_serial_t &s) { data[0] = s.data[0]; }
  inline const Serial &operator = (const qrpc_serial_t &s) {
    data[0] = s.data[0];
    return *this;
  }
  inline bool operator == (const qrpc_serial_t &s) const {
    return Serial::IsSame(*this, s);
  }
  inline bool operator != (const qrpc_serial_t &s) const {
    return !(operator == (s));
  }
  inline bool operator < (const Serial& src) const {
    return Serial::Compare(*this, src);
  }
  inline bool IsEmpty() const {
    return Serial::IsEmpty(*this);
  }
  inline void Clear() {
    Serial::Clear(*this);
  }
  inline const std::string Dump() const {
    return Serial::Dump(*this);
  }
};

template <class H, class P>
inline H MakeHandle(P *p, const Serial &s) {
  H h;
  h.p = p;
  h.s = s;
  return h;
}
}
