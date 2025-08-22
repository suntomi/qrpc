#pragma once

#include "base/session.h"

#include "RTC/SctpDictionaries.hpp"

#include <stdint.h>
#include <functional>

namespace base {
  class Connection;
  class Stream {
  public:
    static std::string SYSCALL_NAME;
    typedef struct {
      // TODO: use general stream parameter struct, instead of borrow from WebRTC
      RTC::SctpStreamParameters params;
      std::string label;
      std::string protocol; // now not used
    } Config;
    typedef Session::CloseReason CloseReason;
    typedef uint16_t Id;
    typedef std::function<int (Stream &, const char *, size_t)> Handler;
  public:
    Stream(Connection &c, const Config &config, bool binary_payload = true) :
      conn_(c), config_(config), binary_payload_(binary_payload ? 1 : 0) {}
    virtual ~Stream() {}
    const Config &config() const { return config_; }
    bool closed() const { return close_reason_ != nullptr; }
    bool reset() const { return reset_ != 0; }
    bool published() const { return published_ != 0; }
    bool binary_payload() const { return binary_payload_ != 0; }
    Id id() const { return config_.params.streamId; }
    const std::string &label() const { return config_.label; }
    Connection &connection() { return conn_; }
    template <class T> const T &context() const { return *static_cast<T *>(context_); }
    template <class T> T &context() { return *static_cast<T *>(context_); }
    static inline bool IsSystem(const std::string &label) { return label[0] == '$'; }
  public:
    virtual int Open();
    virtual void Close(const CloseReason &reason);
    inline void Close(
        qrpc_close_reason_code_t code, int64_t detail_code = 0, const std::string &msg = ""
    ) {
        Close( { .code = code, .detail_code = detail_code, .msg = msg });
    }
    virtual int Send(const char *data, size_t sz);
    inline int Send(const json &&j) {
      auto data = j.dump();
      return Send(data.c_str(), data.length());
    }
    virtual int OnConnect() { return QRPC_OK; }
    virtual void OnShutdown() {}
    virtual int OnRead(const char *p, size_t sz) = 0;
    template <class T> void SetContext(T *t) { context_ = t;}
    void SetReset() { reset_ = 1; }
    void SetPublished(bool on) { published_ = (on ? 1 : 0); }
  protected:
    Connection &conn_;
    Config config_;
    void *context_{nullptr};
    std::unique_ptr<CloseReason> close_reason_;
    uint8_t binary_payload_, reset_{0}, published_{0};
  };
  typedef std::function<std::shared_ptr<Stream> (const Stream::Config &, Connection &)> StreamFactory;
  class AdhocStream : public Stream {
  public:
    typedef std::function<int (Stream &)> ConnectHandler;
    typedef std::function<void (Stream &, const CloseReason &)> ShutdownHandler;
  public:
    AdhocStream(Connection &c, const Config &config, Handler &&h) :
      Stream(c, config, false), read_handler_(std::move(h)), connect_handler_(Nop()), shutdown_handler_(Nop()) {}
    AdhocStream(Connection &c, const Config &config, Handler &&rh, ConnectHandler &&ch, ShutdownHandler &&sh) :
        Stream(c, config, false), read_handler_(std::move(rh)),
        connect_handler_(std::move(ch)), shutdown_handler_(std::move(sh)) {}
    int OnRead(const char *p, size_t sz) override { return read_handler_(*this, p, sz); }
    int OnConnect() override { return connect_handler_(*this); }
    void OnShutdown() override { return shutdown_handler_(*this, *close_reason_); }
  protected:
    struct Nop {
      int operator()(Stream &) { return QRPC_OK; }
      void operator()(Stream &, const CloseReason &) {}
      int operator()(Stream &, const char *, size_t) { return QRPC_OK; }
    };
  private:
    Handler read_handler_;
    ConnectHandler connect_handler_;
    ShutdownHandler shutdown_handler_;
  };
}
