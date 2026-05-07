#include "base/serial.h"

namespace base {
  base::IdFactory<uint64_t> Serial::id_factory_(1, 1, 0x0000FFFFFFFFFFFF);
}