#pragma once

#include "core/nq_loop.h"
#include "core/nq_config.h"

namespace qrpc {
class ServerLoop : class Loop {
 public:
  ServerLoop() {}
  ~NqServerLoop() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ServerLoop);
};
}
