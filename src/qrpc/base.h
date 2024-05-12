#pragma once

#include "base/conn.h"
#include "base/id_factory.h"
#include "base/logger.h"
#include "base/loop.h"
#include "base/stream.h"

namespace qrpc {
  template <typename T>
  using IdFactory = base::IdFactory<T>;
  using Loop = base::Loop;
  namespace logger = base::logger;
  using Stream = base::Stream;
  using Connection = base::Connection;
}