#pragma once

#include "qrpc.h"
#include "base/timespec.h"
#include "base/allocator.h"
#include "core/serial.h"

namespace qrpc {
class Boxer;
class Loop;
class AlarmInterface {
 public:
  virtual ~NqAlarmInterface() {}
  virtual void OnFire() = 0;
  virtual bool IsNonQuicAlarm() const = 0;
};
class AlarmBase : class AlarmInterface {
 protected:
  qrpc_time_t invocation_ts_;
 public:
  AlarmBase() : invocation_ts_(0) {}
  ~NqAlarmBase() override {}

  void OnFire() override = 0;
  bool IsNonQuicAlarm() const override { return false; }

 public:
  void Start(Loop *loop, qrpc_time_t first_invocation_ts);
  void Stop(Loop *loop);
  void Destroy(Loop *loop) {
    Stop(loop);
    delete this;
  }
 protected:
  void ClearInvocationTS() {
    invocation_ts_ = 0;
  }
};
class Alarm : class AlarmBase {
  qrpc_on_alarm_t cb_;
  Boxer *boxer_;
  Serial alarm_serial_;
 public:
  typedef Allocator<NqAlarm> Allocator;

  Alarm() : AlarmBase(), alarm_serial_() { cb_ = qrpc_closure_empty(); }
  ~NqAlarm() override { alarm_serial_.Clear(); }

  inline void Start(Loop *loop, qrpc_time_t first_invocation_ts, qrpc_on_alarm_t cb) {
    TRACE("NqAlarm Start:%p", this);
    cb_ = cb;
    AlarmBase::Start(loop, first_invocation_ts);
  }
  inline Boxer *boxer() { return boxer_; }
  inline const Serial &alarm_serial() const { return alarm_serial_; }
  inline qrpc_alarm_t ToHandle() { return MakeHandle<qrpc_alarm_t, Alarm>(this, alarm_serial_); }
  AlarmIndex alarm_index() const;
  void InitSerial(const qrpc_serial_t &serial) { alarm_serial_ = serial; }

  // implements AlarmInterface
  void OnFire() override;
  void Exec();

  //implement custom allocator
  void* operator new(std::size_t sz);
  void* operator new(std::size_t sz, Boxer* l);
  void operator delete(void *p) noexcept;
  void operator delete(void *p, Boxer *l) noexcept;
};
}
