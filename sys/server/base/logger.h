#pragma once

#include "base/assert.h"
#include <nlohmann/json.hpp>
#include "base/timespec.h"
#include "base/macros.h"
#include <stdlib.h>

namespace base {
using json = nlohmann::json;
namespace logger {
  using record = nlohmann::json;
  enum class level {
    trace,
    debug,
    info,
    warn,
    error,
    fatal,            
    report,
    max,
  };

  //non inline methods
  extern const std::string log_level_descs_[static_cast<size_t>(level::max)];
  extern level log_level_;
  typedef void (*writer_cb_t)(const char *, size_t);
  void configure(writer_cb_t cb, const std::string &ns, bool manual_flush, level llv);
  const std::string &ns();
  void write(const json &j);
  void flush();
  const char *hexdump(const void *p, size_t len);
  inline bool is_enabled(level lv) { return lv >= log_level_; }

  // common log properties
  inline void fill_props(level lv, json &j) {
    //fill default properties
    long sec, nsec;
    clock::now(sec, nsec);
    char tsbuff[32];
    snprintf(tsbuff, sizeof(tsbuff), "%ld.%09ld", sec, nsec);
    j["__ts"] = tsbuff; //((double)sec) + (((double)nsec) / (1000 * 1000 * 1000));
    j["__lv"] = log_level_descs_[static_cast<size_t>(lv)];
    j["__ns"] = ns();
  }

  inline void fill_props(
    level lv,
    const std::string &file, int line, const std::string &func,
    uint64_t trace_id,
    json &j
  ) {
    //fill default properties
    fill_props(lv, j);
    j["_at"] = file + ":" + std::to_string(line);
    j["_fn"] = func;
    if (trace_id != 0) {
      j["_tid"] = hexdump(reinterpret_cast<char *>(&trace_id), sizeof(trace_id));
    }
  }

  //log variadic funcs
  inline void log(level lv, const json &j) {
    ASSERT(j.is_object() || j.is_string());
    if (!is_enabled(lv)) {
      return;
    }
    if (lv >= level::debug) {
      json &mj = const_cast<json &>(j);
      if (j.is_string()) {
        mj = {
          {"ev", j},
        };
      }
      //fill default properties
      fill_props(lv, mj);
      write(mj);
    } else {
      write(j);
    }
  }

  inline void log(
    level lv, const std::string &file, int line, const std::string &func, uint64_t trace_id,
    const json &j
  ) {
    ASSERT(j.is_object() || j.is_string());
    if (!is_enabled(lv)) {
      return;
    }
    if (lv >= level::debug) {
      json &mj = const_cast<json &>(j);
      if (j.is_string()) {
        mj = {
          {"ev", j},
        };
      }
      //fill default properties
      fill_props(lv, file, line, func, trace_id, mj);
      write(mj);
    } else {
      write(j);
    }
  }

  template<class... Args>
  inline void tracef(
    level lv, const std::string &file, int line, const std::string &func, uint64_t trace_id,
    const char *fmt, const Args... args
  ) {
      char buffer[4096];
      DISABLE_FORMAT_SECURITY_WARNING_PUSH
      snprintf(buffer, sizeof(buffer), fmt, args...);
      DISABLE_FORMAT_SECURITY_WARNING_POP
      log(lv, file, line, func, trace_id, buffer);
  }

  inline void trace(
    level lv, const std::string &file, int line, const std::string &func, uint64_t trace_id,
    const json &j
  ) {
      log(lv, file, line, func, trace_id, j);
  }

  template<class... Args>
  inline void logf(
    level lv, const char *fmt, const Args... args
  ) {
      char buffer[4096];
      DISABLE_FORMAT_SECURITY_WARNING_PUSH
      snprintf(buffer, sizeof(buffer), fmt, args...);
      DISABLE_FORMAT_SECURITY_WARNING_POP
      log(lv, buffer);
  }

  //short hands for each severity
  inline void debug(const json &j) { log(level::debug, j); }
  inline void info(const json &j) { log(level::info, j); }
  inline void warn(const json &j) { log(level::warn, j); }
  inline void error(const json &j) { log(level::error, j); }
  inline void fatal(const json &j) { log(level::fatal, j); }
  inline void report(const json &j) { log(level::report, j); }
  [[noreturn]] inline void die(const json &j, int exit_code = 1) {
    fatal(j);
    ASSERT(false);
    exit(exit_code);
  }
}
}

#define QRPC_LOG(level__, ...) { ::base::logger::tracef(::base::logger::level::level__, __FILE__, __LINE__, __func__, 0, __VA_ARGS__); }
#define QRPC_LOGJ(level__, ...) { ::base::logger::trace(::base::logger::level::level__, __FILE__, __LINE__, __func__, 0, __VA_ARGS__); }
#define QRPC_LOGVJ(level_value__, ...) { ::base::logger::trace(level_value__, __FILE__, __LINE__, __func__, 0, __VA_ARGS__); }
#if defined(VERBOSE)
  #define QRPC_VLOG(level__, ...) { ::base::logger::tracef(::base::logger::level::level__, __FILE__, __LINE__, __func__, 0, __VA_ARGS__); } 
  #define QRPC_VLOGJ(level__, ...) { ::base::logger::trace(::base::logger::level::level__, __FILE__, __LINE__, __func__, 0, __VA_ARGS__); } 
#else
  #define QRPC_VLOG(level__, ...)
  #define QRPC_VLOGJ(level__, ...)  
#endif

#if !defined(TRACE)
  #if !defined(NDEBUG)
    #define TRACE(...) { ::base::logger::tracef(::base::logger::level::trace, __FILE__, __LINE__, __func__, 0, __VA_ARGS__); }
  #else
    #define TRACE(...) // fprintf(stderr, __VA_ARGS__)
  #endif
#endif

#if !defined(TRACEJ)
  #if !defined(NDEBUG)
    #define TRACEJ(...) { ::base::logger::trace(::base::logger::level::trace, __FILE__, __LINE__, __func__, 0, __VA_ARGS__); }
  #else
    #define TRACEJ(...) // fprintf(stderr, __VA_ARGS__)
  #endif
#endif

#if !defined(TRACK)
  #if !defined(NDEBUG) && defined(QRPC_ENABLE_TRACK)
    #define TRACK(...) { ::base::logger::tracef(::base::logger::level::debug, __FILE__, __LINE__, __func__, 0, "track"); }
  #else
    #define TRACK(...) // fprintf(stderr, __VA_ARGS__)
  #endif
#endif


#if !defined(DIE)
  #define DIE(msg) { ::base::logger::die(msg); }
#endif