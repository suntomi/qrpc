#pragma once

#include <string>

#include "moodycamel/concurrentqueue.h"

#include "qrpc.h"
#include "base/allocator.h"
#include "core/nq_closure.h"
#include "core/nq_session_delegate.h"
#include "core/nq_stream.h"
#include "core/nq_alarm.h"
#include "core/serial.h"

//#define USE_DIRECT_WRITE (1)

namespace qrpc {
class Loop;
class Boxer {
 public:
  enum UnboxResult {
    Ok = 0,
    NeedTransfer = -1, //target handle lives in other thread
    SerialExpire = -2,    
  };
  enum OpCode : uint8_t {
    Nop = 0,
    Disconnect,
    Reconnect,
    DoReconnect,
    Flush,
    Finalize,
    Start,
    OpenStream,
    Task,
    Send,
    SendEx,
    Call,
    CallEx,
    Reply,
    Notify,
    Exec,
    Reachability,
    ModifyHandlerMap,
  };
  enum OpTarget : uint8_t {
    Invalid = 0,
    Conn = 1,
    Stream = 2,
    Alarm = 3,
  };
  struct Op {
    qrpc_serial_t serial_;
    void *target_ptr_;
    OpCode code_;
    OpTarget target_; 
    uint8_t padd_[2];
    struct Data {
      const void *p_;
      qrpc_size_t len_;
      Data() { len_ = 0; }
      Data(const void *p, qrpc_size_t len) {
        if (len <= 0) {
          p_ = p; len_ = len; //if p is not byte array, assume memory is managed by caller
        } else {
          ASSERT(len <= 10000);
          //TODO(iyatomi): allocation from some pool
          p_ = Syscall::Memdup(p, len); len_ = len;
        }
      }
      ~Data() { if (len_ > 0) { Syscall::MemFree(const_cast<void *>(p_)); } }
      inline const void *ptr() const { return p_; }
      inline qrpc_size_t length() const { return len_; } 
    } data_;
    union {
      struct {
        qrpc_close_reason_code_t reason;
      } disconnect_;
      struct {
        qrpc_on_rpc_reply_t on_reply_;
        uint16_t type_;
      } call_;
      struct {
        qrpc_rpc_opt_t rpc_opt_;
        uint16_t type_;
      } call_ex_;
      struct {
        qrpc_stream_opt_t stream_opt_;
      } send_ex_;
      struct {
        uint16_t type_;
      } notify_;
      struct {
        qrpc_error_t result_;
        qrpc_msgid_t msgid_;
      } reply_;
      struct {
        qrpc_time_t invocation_ts_;
        qrpc_on_alarm_t callback_;
      } alarm_;
      struct {
        char *name_;
        void *ctx_;
      } stream_;
      struct {
        qrpc_closure_t callback_;
      } task_;
      struct {
        qrpc_reachability_t state_;
      } reachability_;
    };
    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_() {}

    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, qrpc_time_t ts, qrpc_on_alarm_t cb, 
      OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_() {
      alarm_.invocation_ts_ = ts;
      alarm_.callback_ = cb;
    }

    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, qrpc_closure_t cb, OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_() {
      task_.callback_ = cb;
    }

    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, const char *name, void *ctx, 
      OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_() {
      if (*name != 0) {
        stream_.name_ = strdup(name);
        stream_.ctx_ = ctx;
      }
    }

    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, qrpc_close_reason_code_t reason, 
      const uint8_t *detail, qrpc_size_t detail_len,
      OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(detail, detail_len) {
      disconnect_.reason = reason;
    }

    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, qrpc_reachability_t state, 
      OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_() {
      reachability_.state_ = state;
    }

    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, const void *data, qrpc_size_t datalen, 
       OpTarget target = OpTarget::Stream) : 
      serial_(serial), code_(code), target_(target), data_(data, datalen) {}
    
    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, const void *data, qrpc_size_t datalen,
       const qrpc_stream_opt_t &opt, OpTarget target = OpTarget::Stream) : 
      serial_(serial), code_(code), target_(target), data_(data, datalen) {
      send_ex_.stream_opt_ = opt;
    }
    
    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, uint16_t type, const void *data, 
       qrpc_size_t datalen, qrpc_on_rpc_reply_t on_reply, 
       OpTarget target = OpTarget::Stream) :
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(data, datalen) {
      call_.type_ = type;
      call_.on_reply_ = on_reply;
    }
    
    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, uint16_t type, const void *data, 
       qrpc_size_t datalen, const qrpc_rpc_opt_t &rpc_opt, 
       OpTarget target = OpTarget::Stream) :
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(data, datalen) {
      call_ex_.type_ = type;
      call_ex_.rpc_opt_ = rpc_opt;
    }
    
    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, uint16_t type, 
       const void *data, qrpc_size_t datalen, OpTarget target = OpTarget::Stream) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(data, datalen) {
      notify_.type_ = type;
    }
    
    Op(const qrpc_serial_t &serial, void *target_ptr, OpCode code, 
       qrpc_error_t result, qrpc_msgid_t msgid, 
       const void *data, qrpc_size_t datalen, OpTarget target = OpTarget::Stream) :
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(data, datalen) {
      reply_.result_ = result;
      reply_.msgid_ = msgid;
    }
    ~Op() {}
  };
  class Processor : public moodycamel::ConcurrentQueue<Op*> {
  public:
    void Poll(Boxer *p);
  };

  //interfaces
  virtual void Enqueue(Op *op) = 0;
  virtual bool MainThread() const = 0;
  virtual Loop *Loop() = 0;
  virtual Alarm *NewAlarm() = 0;
  virtual Alarm::Allocator *GetAlarmAllocator() = 0;
  virtual void RemoveAlarm(AlarmIndex index) = 0;
  virtual bool IsClient() const = 0;
  virtual bool IsSessionLocked(SessionIndex idx) const = 0;
  virtual void LockSession(SessionIndex idx) = 0;
  virtual void UnlockSession() = 0;

  //invoker
  inline void InvokeConn(const qrpc_serial_t &serial, SessionDelegate *unboxed, OpCode code, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->SessionSerial() == serial) {
        switch (code) {
        case DoReconnect:
          unboxed->DoReconnect();
          break;
        case Finalize:
          unboxed->Destroy();
          break;
        case Flush: {
          unboxed->FlushWriteBuffer();
        } break;
        default:
          ASSERT(false);
          return;
        }
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, OpTarget::Conn));      
    }
  }
  inline void InvokeConn(const qrpc_serial_t &serial, SessionDelegate *unboxed, OpCode code, 
                         qrpc_close_reason_code_t reason, const uint8_t *detail, qrpc_size_t detail_len, bool from_queue = false) {
    if (from_queue) {
      if (unboxed->SessionSerial() == serial) {
        switch (code) {
          case Disconnect:
            unboxed->Disconnect(reason, detail, detail_len);
            break;
          case Reconnect:
            unboxed->Reconnect(true, reason, detail, detail_len);
            break;
          default:
            ASSERT(false);
            return;
        }
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, reason, detail, detail_len, OpTarget::Conn));      
    }
  }
  inline void InvokeConn(const qrpc_serial_t &serial, SessionDelegate *unboxed, OpCode code, const char *name, void *ctx, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->SessionSerial() == serial) {
        unboxed->OpenStream(name, ctx);
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, name, ctx, OpTarget::Conn));      
    }
  }
  inline void InvokeConn(const qrpc_serial_t &serial, SessionDelegate *unboxed, OpCode code, qrpc_reachability_t state, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->SessionSerial() == serial) {
        ASSERT(code == Reachability);
        unboxed->OnReachabilityChange(state);
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, state, OpTarget::Conn));      
    }
  }
  inline void InvokeConn(const qrpc_serial_t &serial, SessionDelegate *unboxed, OpCode code, qrpc_closure_t cb, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->SessionSerial() == serial) {
        ASSERT(code == ModifyHandlerMap);
        auto hm = unboxed->ResetHandlerMap()->ToHandle();
        qrpc_dyn_closure_call(cb, on_conn_modify_hdmap, hm);
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, cb, OpTarget::Conn));      
    }
  }
  inline void InvokeAlarm(const qrpc_serial_t &serial, Alarm *unboxed, OpCode code, qrpc_time_t invocation_ts, qrpc_on_alarm_t cb, bool from_queue = false) {
    if (from_queue) {
      if (unboxed->alarm_serial() == serial) {
        ASSERT(code == Start);
        unboxed->Start(Loop(), invocation_ts, cb);
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, invocation_ts, cb, OpTarget::Alarm));
    }    
  }
  inline void InvokeAlarm(const qrpc_serial_t &serial, Alarm *unboxed, OpCode code, bool from_queue = false) {
    if (from_queue) {
      if (unboxed->alarm_serial() == serial) {
        switch (code) {
        case Finalize:
          RemoveAlarm(unboxed->alarm_index());
          unboxed->Destroy(Loop());
          break;
        case Exec:
          unboxed->Exec();
          break;
        default:
          ASSERT(false);
          break;
        }
      } else {
        //already got invalid
        ASSERT(false);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, OpTarget::Alarm));
    }    
  }
  inline void InvokeStream(const qrpc_serial_t &serial, Stream *unboxed, OpCode code, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Disconnect);
        unboxed->Disconnect();
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, OpTarget::Stream));
    }
  }
  inline void InvokeStream(const qrpc_serial_t &serial, Stream *unboxed, OpCode code, qrpc_closure_t cb, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Task);
        unboxed->RunTask(cb);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, cb, OpTarget::Stream));
    }
  }
  inline void InvokeStream(const qrpc_serial_t &serial, Stream *unboxed, OpCode code, 
                           const void *data, qrpc_size_t datalen, bool from_queue = false) {
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Send);
        unboxed->Handler<NqStreamHandler>()->Send(data, datalen);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, data, datalen));
    }
  }
  inline void InvokeStream(const qrpc_serial_t &serial, Stream *unboxed, OpCode code, 
                           const void *data, qrpc_size_t datalen, qrpc_stream_opt_t &stream_opt, 
                           bool from_queue = false) {
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Send);
        unboxed->Handler<NqStreamHandler>()->SendEx(data, datalen, stream_opt);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, data, datalen, stream_opt));
    }
  }
  inline void InvokeStream(const qrpc_serial_t &serial, Stream *unboxed, OpCode code,
                           uint16_t type, const void *data, 
                           qrpc_size_t datalen, qrpc_on_rpc_reply_t on_reply, bool from_queue = false) {
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Call);
        unboxed->Handler<NqSimpleRPCStreamHandler>()->Call(type, data, datalen, on_reply);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, type, data, datalen, on_reply));
    }
  }
  inline void InvokeStream(const qrpc_serial_t &serial, Stream *unboxed, OpCode code, 
                           uint16_t type, const void *data, 
                           qrpc_size_t datalen, qrpc_rpc_opt_t &rpc_opt, bool from_queue = false) {
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == CallEx);
        unboxed->Handler<NqSimpleRPCStreamHandler>()->CallEx(type, data, datalen, rpc_opt);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, type, data, datalen, rpc_opt));
    }
  }
  inline void InvokeStream(const qrpc_serial_t &serial, Stream *unboxed, OpCode code, 
                           uint16_t type, const void *data, qrpc_size_t datalen, bool from_queue = false) {    
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Notify);
        unboxed->Handler<NqSimpleRPCStreamHandler>()->Notify(type, data, datalen);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, type, data, datalen));
    }
  }
  inline void InvokeStream(const qrpc_serial_t &serial, Stream *unboxed, OpCode code, 
                           qrpc_error_t result, qrpc_msgid_t msgid, 
                           const void *data, qrpc_size_t datalen, bool from_queue = false) {
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Reply);
        unboxed->Handler<NqSimpleRPCStreamHandler>()->Reply(result, msgid, data, datalen);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, result, msgid, data, datalen));
    }
  }

  
  template <class H>
  struct unbox_result_trait {
    typedef nullptr_t value;
  };
  template <class H>
  static inline typename unbox_result_trait<H>::value *Unbox(H h) {
    typename unbox_result_trait<H>::value *r;
    Boxer *boxer = From(h);
    return boxer->Unbox(h.s, &r) == UnboxResult::Ok ? r : nullptr;
  }
};
template <>
struct Boxer::unbox_result_trait<qrpc_conn_t> {
  typedef SessionDelegate value;
};
template <>
struct Boxer::unbox_result_trait<qrpc_stream_t> {
  typedef Stream value;
};
template <>
struct Boxer::unbox_result_trait<qrpc_rpc_t> {
  typedef Stream value;
};
}
