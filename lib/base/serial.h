#pragma once

#include <map>
#include <mutex>
#include <algorithm>

#include <inttypes.h>

#include "base/defs.h"
#include "base/id_factory.h"

namespace base {
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
  typedef uint16_t PartitionId;
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
public:
  inline Serial(PartitionId id) { data[0] = MakeSerial(id); }
  inline Serial(const qrpc_serial_t *s) { data[0] = s->data[0]; }
  inline PartitionId partition_id() const { return data[0] >> ((sizeof(data[0]) - sizeof(PartitionId)) << 3); }
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
  static inline PartitionId GetPartitionId(const qrpc_serial_t &s) {
    return s.data[0] >> ((sizeof(s.data[0]) - sizeof(PartitionId)) << 3);
  }
  static inline uint64_t MakeSerial(PartitionId id) {
    ASSERT(id != 0);
    return ((uint64_t(id) << ((sizeof(data[0]) - sizeof(PartitionId)) << 3)) | id_factory_.New());
  }
protected:
  static base::IdFactory<uint64_t> id_factory_;
};

template <class H, class P>
inline H MakeHandle(P *p, const Serial &s) {
  H h;
  h.p = p;
  h.s = s;
  return h;
}
}
