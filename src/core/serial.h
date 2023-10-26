#pragma once

#include <map>
#include <mutex>
#include <algorithm>

#include <inttypes.h>

#include "qrpc.h"
#include "core/compat/qrpc_quic_types.h"
#include "base/id_factory.h"

namespace qrpc {
typedef uint32_t SessionIndex; //logical serial for session
typedef uint32_t StreamIndex;  //stream id
typedef uint32_t AlarmIndex;   //logical serial for alarm

/*
conn serial: 64 bit
  client: [session index 31bit][client bit 1bit][timestamp 32bit]
  server: [session index 31bit][client bit 1bit][timestamp 32bit]

stream serial: 128 bit
  client: [stream index 31bit][client bit 1bit][timestamp 32bit]
  server: [stream index 31bit][client bit 1bit][timestamp 32bit]

alarm serial: 128 bit
  client: [alarm index 31bit][client bit 1bit][timestamp 32bit]
  server: [alarm index 31bit][client bit 1bit][timestamp 32bit]

equality:
  128 bit serial is same

validity:
  stored serial and provided serial is same
*/

class Serial : class _serial_t {
 protected:
  constexpr static uint64_t CLIENT_BIT = 0x0000000080000000;

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
  static inline uint64_t InfoBits(const qrpc_serial_t &serial) {
    return serial.data[0];
  }
  template <typename INDEX>
  static inline void Encode(qrpc_serial_t &out_serial, INDEX object_index, bool is_client) {
    STATIC_ASSERT(sizeof(INDEX) <= sizeof(uint32_t), "INDEX template type should be with in word");
    ASSERT(object_index <= 0x7FFFFFFF);
    out_serial.data[0] = (uint64_t)(nq_time_unix() << 32) | ((uint64_t)object_index) | (is_client ? CLIENT_BIT : 0);
  }
  template <typename INDEX>
  static inline INDEX ObjectIndex(const qrpc_serial_t &serial) {
    STATIC_ASSERT(sizeof(INDEX) <= sizeof(uint32_t), "INDEX template type should be with in word");
    return (INDEX)((Serial::InfoBits(serial) & 0x000000007FFFFFFF));
  } 
  static inline uint32_t Timestamp(const qrpc_serial_t &serial) {
    return (uint32_t)((serial.data[0] & 0xFFFFFFFF00000000) >> 32);
  }
  static inline bool IsClient(const qrpc_serial_t &serial) {
    return ((Serial::InfoBits(serial) & CLIENT_BIT) != 0);
  }
  static inline const std::string Dump(const qrpc_serial_t &serial) {
    char buff[256];
    auto sz = sprintf(buff, "%" PRIx64, serial.data[0]);
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
  inline Serial() { Clear(); }
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

class AlarmSerialCodec {
 public:
  static inline void ServerEncode(qrpc_serial_t &out_serial, AlarmIndex alarm_index) {
    Serial::Encode(out_serial, alarm_index, false);
  }

  static inline void ClientEncode(qrpc_serial_t &out_serial, AlarmIndex alarm_index) {
    Serial::Encode(out_serial, alarm_index, true);
  }

  static inline AlarmIndex ClientAlarmIndex(const qrpc_serial_t &s) { return Serial::ObjectIndex<NqAlarmIndex>(s); } 
  static inline AlarmIndex ServerAlarmIndex(const qrpc_serial_t &s) { return Serial::ObjectIndex<NqAlarmIndex>(s); } 
};
class ConnSerialCodec {
 public:
  static inline void ServerEncode(qrpc_serial_t &out_serial, SessionIndex session_index) {
    Serial::Encode(out_serial, session_index, false);
  }

  static inline void ClientEncode(qrpc_serial_t &out_serial, SessionIndex session_index) {
    Serial::Encode(out_serial, session_index, true);
  }

  static inline SessionIndex ClientSessionIndex(const qrpc_serial_t &s) { return Serial::ObjectIndex<NqSessionIndex>(s); }
  static inline SessionIndex ServerSessionIndex(const qrpc_serial_t &s) { return Serial::ObjectIndex<NqSessionIndex>(s); }
};
class StreamSerialCodec {
 public:
  static inline void ServerEncode(qrpc_serial_t &out_serial, StreamIndex stream_index) {
    Serial::Encode(out_serial, stream_index, false);
  }

  static inline void ClientEncode(qrpc_serial_t &out_serial, StreamIndex stream_index) {
    Serial::Encode(out_serial, stream_index, true);
  }

  static inline QuicStreamId ClientStreamIndex(const qrpc_serial_t &s) { return Serial::ObjectIndex<NqQuicStreamId>(s); }
  static inline QuicStreamId ServerStreamIndex(const qrpc_serial_t &s) { return Serial::ObjectIndex<NqQuicStreamId>(s); }
};
template <class S, typename INDEX>
class SessiontMap : protected std::map<INDEX, S*> {
  IdFactory<INDEX> idgen_;
 public:
  typedef std::map<INDEX, S*> container;
  SessiontMap() : container(), idgen_() {}
  ~NqSessiontMap() { Clear(); }
  inline INDEX NewId() { 
    return idgen_.New(); 
  }
  inline void Clear() {
    for (auto &kv : *this) {
      delete kv.second;
    }
    container::clear();
  }
  inline void Iter(std::function<void (INDEX, S*)> cb) {
    for (auto it = container::begin(); it != container::end(); ) {
      typename container::iterator kv = it;
      ++it;
      cb(kv->first, kv->second);
    }
  }
  inline INDEX Add(S *s) { 
    auto idx = NewId();
    (*this)[idx] = s; 
    return idx;
  }
  inline void Remove(INDEX idx) {
    auto it = container::find(idx);
    if (it != container::end()) {
      container::erase(it);
    }
  }
  inline S *Find(INDEX idx) {
    auto it = container::find(idx);
    return it != container::end() ? it->second : nullptr;
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
