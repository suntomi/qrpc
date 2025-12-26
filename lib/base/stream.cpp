#include "base/stream.h"
#include "base/conn.h"

namespace base {
  std::string Stream::SYSCALL_NAME = "$syscall";
  Stream::Stream(Connection &c, const Config &config, bool binary_payload) :
    conn_(c), config_(config), serial_(
      base::Serial::GetPartitionId(c.serial())
    ), binary_payload_(binary_payload ? 1 : 0) {}
  int Stream::Open() {
    return conn_.Open(*this);
  }
  void Stream::Close(const CloseReason &reason) {
    if (!closed()) {
      close_reason_ = std::make_unique<CloseReason>(reason);
      conn_.Close(*this);
    }
  }
  int Stream::Send(const char *data, size_t sz) {
    return conn_.Send(*this, data, sz, binary_payload());
  }
}