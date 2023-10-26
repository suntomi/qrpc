#include "core/nq_alarm.h"

#include "core/nq_boxer.h"

namespace qrpc {
// AlarmBase
void AlarmBase::Start(Loop *loop, qrpc_time_t first_invocation_ts) {
  Stop(loop);
  ASSERT(invocation_ts_ == 0);
  invocation_ts_ = first_invocation_ts;
  loop->SetAlarm(this, clock::to_us(invocation_ts_));
}
void AlarmBase::Stop(Loop *loop) {
  if (invocation_ts_ != 0) {
    loop->CancelAlarm(this, clock::to_us(invocation_ts_));
    invocation_ts_ = 0;
  }
}



// Alarm
AlarmIndex Alarm::alarm_index() const {
  if (Serial::IsClient(alarm_serial_)) {
    return AlarmSerialCodec::ClientAlarmIndex(alarm_serial_);
  } else {
    return AlarmSerialCodec::ServerAlarmIndex(alarm_serial_);    
  }
}
void* Alarm::operator new(std::size_t sz) {
  ASSERT(false);
  auto r = reinterpret_cast<NqAlarm *>(std::malloc(sz));
  r->boxer_ = nullptr;
  return r;
}
void* Alarm::operator new(std::size_t sz, Boxer* b) {
  auto r = reinterpret_cast<NqAlarm *>(b->GetAlarmAllocator()->Alloc(sz));
  r->boxer_ = b;
  return r;
}
void Alarm::operator delete(void *p) noexcept {
  auto r = reinterpret_cast<NqAlarm *>(p);
  if (r->boxer_ == nullptr) {
    ASSERT(false);
    std::free(r);
  } else {
    r->boxer_->GetAlarmAllocator()->Free(p);
  }
}
void Alarm::operator delete(void *p, Boxer *b) noexcept {
  b->GetAlarmAllocator()->Free(p);
}
void Alarm::OnFire() {
  boxer_->InvokeAlarm(alarm_serial_, this, Boxer::OpCode::Exec);
  //ClearInvocationTS();
}
void Alarm::Exec() {
  //here, alarm is already unregistered from Loop::alarm_map_
  Loop *loop = boxer_->Loop();
  qrpc_time_t invoke = invocation_ts_;
  qrpc_time_t next = invoke;
  qrpc_closure_call(cb_, &next);
  ClearInvocationTS();
  if (next > invoke) {
    AlarmBase::Start(loop, next);
    ASSERT(invocation_ts_ == next);
  } else if (next == 0) {
    //stopped but not freed. you can resume by using qrpc_alarm_set
  } else {
    delete this;
  }    
}
}
