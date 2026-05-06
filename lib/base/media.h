#pragma once

#include <string>

namespace base {
  class Connection;
  class Media {
  public:
    typedef std::string Id; // label of the media
    typedef std::string Rid; // Rid of RTP stream
    typedef std::string Mid; // Rid of RTP stream
    typedef std::string TrackId; // track id of WebRTC js API
    typedef std::string ScalabilityMode; // scalability mode of RTP stream (SVC)
    typedef uint64_t Ssrc; // Ssrc of RTP stream
  public:
    Media(const std::string &path, Connection &c)
      : path_(path), conn_(c) {
        qrpc_closure_init_noop(consumer_, qrpc_on_media_consume_t);
      }
    virtual ~Media() = default;
    virtual int OnOpen() { return QRPC_OK; }
    virtual void OnClose() {}
    virtual void OnStateChange(const char *ev, const char *reason) {}
    virtual void OnRtpPacketReceived(const char *, size_t) {}
  public:
    Connection &connection() { return conn_; }
    const std::string &path() const { return path_; }
    void *context() { return context_; }
    void SetWatcher(qrpc_on_media_consume_t cb) { consumer_ = cb; }
  protected:
    std::string path_;
    qrpc_on_media_consume_t consumer_;
    base::Connection &conn_;
    void *context_;
  };
}
