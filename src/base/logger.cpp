#include "base/logger.h"
#include <mutex>
#include <moodycamel/concurrentqueue.h>
#if defined(NO_LOG_WRITE_CALLBACK)
#include <iostream>
#endif

namespace base {
namespace logger {
  const std::string log_level_descs_[static_cast<size_t>(level::max)] = {
      "T",
      "D",
      "I",
      "W",
      "E",
      "F",
      "R",
  };
#if defined(NDEBUG)
  level log_level_ = level::info;
#else
  level log_level_ = level::debug;
#endif
  static inline void default_writer(const char *buf, size_t len) {
    fwrite(buf, 1, len, stderr);
    fputc('\n', stderr);
  }
  static writer_cb_t writer_ = default_writer;
  static std::string namespace_ = "qrpc";
  static std::mutex mtx_;
  static bool manual_flush_ = false;
  void configure(writer_cb_t cb, const std::string &ns, bool manual_flush, level llv) {
      if (cb != nullptr) {
      writer_ = cb;
    }
    if (ns.length() > 0) {
      namespace_ = ns;
    }
    if (llv != level::max) {
      log_level_ = llv;
    }
    manual_flush_ = manual_flush;
  }
  const std::string &ns() { return namespace_; }

#if !defined(NO_LOG_WRITE_CALLBACK)
  static moodycamel::ConcurrentQueue<std::string> s_logs;
  void flush() {
//  printf("flush_from_main_thread");
    if (manual_flush_) {
      std::string str;
      while (s_logs.try_dequeue(str)) {
        writer_(str.c_str(), str.length());
      }
    }
  }
#else
  void flush() {
  }
#endif

  void write(const json &j) {
    mtx_.lock();
#if defined(NO_LOG_WRITE_CALLBACK)
    std::err << j << std::endl;
#else
    auto body = j.dump();
    if (manual_flush_) {
      s_logs.enqueue(body);
    } else {
      writer_(body.c_str(), body.length());     
    }
#endif
    mtx_.unlock();
  }

  thread_local char buffer[64 + 1];
  static constexpr char hex[] = "0123456789abcdef";
  const char *hexdump(const void *p, size_t len) {
    size_t i = 0;
    const char *cp = reinterpret_cast<const char *>(p);
    for (i = 0; i < len && i < (sizeof(buffer) >> 1); i++) {
      auto v = cp[i];
      buffer[2 * i] = hex[v >> 4];
      buffer[2 * i + 1] = hex[v & 0xf];
    }
    buffer[2 * i + 1] = 0;
    return buffer;
  }
}
}
