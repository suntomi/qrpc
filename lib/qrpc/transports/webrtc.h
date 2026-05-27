#pragma once

#include "base/webrtc.h"
#include "base/string.h"
#include "base/serial.h"
#include "base/allocator.h"

#include "qrpc/base.h"
#include "qrpc/client.h"
#include "qrpc/conn.h"
#include "qrpc/listener.h"
#include "qrpc/loop.h"
#include "qrpc/server.h"
#include "qrpc/stream.h"
#include "qrpc/media.h"

namespace qrpc {
using ListenerInterface = Listener;
using ClientInterface = Client;
namespace webrtc {
  using ConnectionFactory = base::webrtc::ConnectionFactory;
  using DtlsTransport = RTC::DtlsTransport;
  class Client;

  template <class T>
  using HandleAllocator = base::SharedPtrAllocator<T, HandleBss>;

  struct StreamAllocators {
    HandleAllocator<ByteStream> byte;
    HandleAllocator<RPCStream> rpc;

    StreamAllocators(size_t chunk_size) :
      byte(chunk_size), rpc(chunk_size) {}
  };

  template <class ConnectionT>
  class HandleAllocators {
  public:
    HandleAllocators(size_t conn_chunk_size, size_t stream_chunk_size, size_t media_chunk_size) :
      conn_(conn_chunk_size), streams_(stream_chunk_size), media_(media_chunk_size) {}
    template <class... Args>
    std::shared_ptr<ConnectionT> NewConnection(Args&&... args) {
      return conn_.Alloc(std::forward<Args>(args)...);
    }
    inline std::shared_ptr<Stream> NewStream(
      const Stream::Config &c, base::Connection &conn, const qrpc_handler_entry_t &he
    ) {
      // webrtc has message boundary (via SCTP), so always create non coded stream
      switch (he.type) {
      case qrpc_handler_type_t::STREAM: {
        return streams_.byte.Alloc(conn, c, he.stream);
      } break;
      case qrpc_handler_type_t::RPC: {
        return streams_.rpc.Alloc(conn, c, he.rpc, conn.alarm_processor());
      } break;
      default:
        ASSERT(false);
        return nullptr;
      }
    }
    inline std::shared_ptr<base::Media> NewMedia(
      const std::string &path, base::Media::Direction direction, base::Connection &conn, qrpc_media_handler_t &handler
    ) {
      return media_.Alloc(path, direction, conn, handler);
    }
  private:
    HandleAllocator<ConnectionT> conn_;
    StreamAllocators streams_;
    HandleAllocator<Media> media_;
  };

  // webrtc::ServerConnection
  class ServerConnection : public qrpc::Connection, public base::webrtc::Listener::Connection {
  public:
    ServerConnection(ConnectionFactory &cf, DtlsTransport::Role dtls_role, const qrpc_listen_conf_t &config) :
      qrpc::Connection(dynamic_cast<qrpc::Loop &>(cf.loop()).partition_id()),
      base::webrtc::Listener::Connection(cf, dtls_role),
      on_open_(config.on_open), on_close_(config.on_close), ctx_(nullptr) {}
    int OnConnect() override { return qrpc_closure_call(on_open_, ToHandle(), &ctx_); }
    qrpc_time_t OnShutdown() override { 
      qrpc_closure_call(on_close_, ToHandle(), &close_reason_->To(), close_reason_->code == QRPC_CLOSE_REASON_REMOTE);
      return qrpc_alarm_stop_rv();
    }
    void *context() override { return ctx_; }
    std::shared_ptr<base::Media> media_factory(const std::string &path, base::Media::Direction direction) override;
  protected:
    void *ctx_;
    qrpc_on_server_conn_open_t on_open_;
    qrpc_on_server_conn_close_t on_close_;
  };

  // webrtc::Listener
  class Listener : public base::webrtc::Listener, public ListenerInterface {
  public:
    Listener(Worker &w, int port_index, const qrpc_listen_conf_t &config) :
      base::webrtc::Listener(
        w.loop(),
        // configurations
        base::webrtc::Listener::Config::From(
          config.transport.webrtc.params.session_timeout,
          config.transport.webrtc.params.connection_timeout
        ),
        base::webrtc::Listener::TransportConfig::From(config.transport),
        // connection factory method
        [this](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
          return allocators_.NewConnection(cf, role, config_);
        },
        // stream factory
        [this](const Stream::Config &c, base::Connection &conn) {
          auto &sc = dynamic_cast<ServerConnection &>(conn);
          auto he = qrpc_closure_call(config_.stream_router, c.label.c_str(), sc.ToHandle());
          if (he == nullptr) {
            logger::die({{"ev","no handler found"},{"label",c.label},{"ptr",base::str::dptr(&conn)}});
          }
          return allocators_.NewStream(c, conn, *he);
        }
      ), worker_(w), config_(config),
      allocators_(config.conn_chunk_size, config.stream_chunk_size, config.media_chunk_size),
      port_index_(port_index) {}
    ~Listener() override {
      // it called from parent class' destructor but it assume that our allocators are alive, so parent's Fin() first, 
      // then free allocators
      base::webrtc::Listener::Fin();
    }
    qrpc_transport_type_t transport_type() const override { return QRPC_TRANSPORT_WEBRTC; }
  public:
    bool Listen(int signaling_port, const qrpc_endpoint_t &ep) {
      return base::webrtc::Listener::Listen(signaling_port, base::webrtc::Listener::Endpoint::From(ep));
    }
    inline const qrpc_listen_conf_t &config() const { return config_; }
    HandleAllocators<ServerConnection> &allocator() { return allocators_; }
  private:
    Worker &worker_;
    qrpc_listen_conf_t config_;
    HandleAllocators<ServerConnection> allocators_;
    int port_index_;
  };

    // webrtc::ClientConnection
  class ClientConnection : public qrpc::Connection, public base::webrtc::Client::Connection {
  public:
    ClientConnection(
      ConnectionFactory &cf, DtlsTransport::Role dtls_role,
      base::Serial::PartitionId pid, const qrpc_connect_conf_t &conf, base::StreamFactory &&sf
    ) : qrpc::Connection(pid),
      base::webrtc::Client::Connection(
        cf, dtls_role, base::webrtc::Client::TransportConfig::From(conf.transport), std::move(sf)
      ),
      config_(conf), on_open_(conf.on_open), on_close_(conf.on_close), on_finalize_(conf.on_finalize) {}
    ~ClientConnection() override { qrpc_closure_call(on_finalize_, ToHandle()); }
    int OnConnect() override { return qrpc_closure_call(on_open_, ToHandle(), &ctx_); }
    qrpc_time_t OnShutdown() override {
      return qrpc_closure_call(on_close_, ToHandle(), &close_reason_->To(), close_reason_->code == QRPC_CLOSE_REASON_REMOTE);
    }
    void *context() override { return ctx_; }
    std::shared_ptr<base::Media> media_factory(const std::string &path, base::Media::Direction direction) override;
  protected:
    void *ctx_;
    qrpc_connect_conf_t config_;
    qrpc_on_client_conn_open_t on_open_;
    qrpc_on_client_conn_close_t on_close_;
    qrpc_on_client_conn_finalize_t on_finalize_;
  };

  // webrtc::Client
  class Client : public qrpc::Loop, public ClientInterface {
  public:
    // Loop(true) does auto assignment of partition_id.
    Client(const qrpc_clconf_t &config) : Loop(true), resolver_(*this), config_(config), transport_(
      OpenOrDie(config.max_nfd, config.poll_timeout_ns),
      base::webrtc::Client::Config::From(
        resolver_.InitOrDie(AsyncResolver::Config::From(config.dns)), config.session_timeout, config.connect_timeout
      )), allocators_(config.conn_chunk_size, config.stream_chunk_size, config.media_chunk_size), queue_() {}
    ~Client() override {
      // it called from parent transport_'s destructor but it assume that our allocators are alive, so transport_'s Fin() first, 
      // then free allocators
      transport_.Fin();
    }
    void Close(base::Connection &c) override { transport_.Close(c); }
    bool Connect(const qrpc_connect_conf_t &c) override {
      return transport_.Connect(
        base::webrtc::Client::Endpoint::From(c.ep),
        [this, conf = c, pid = partition_id()](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
          base::StreamFactory sf = [this, conf](const base::Stream::Config &sc, base::Connection &conn) -> std::shared_ptr<base::Stream> {
            auto &cc = dynamic_cast<ClientConnection &>(conn);
            auto he = qrpc_closure_call(conf.stream_router, sc.label.c_str(), cc.ToHandle());
            if (he == nullptr) {
              logger::die({{"ev","no handler found"},{"label",sc.label},{"ptr",base::str::dptr(&conn)}});
            }
            return allocators_.NewStream(sc, conn, *he);
          };
          return allocators_.NewConnection(cf, role, pid, conf, std::move(sf));
        }
      );
    }
    HandleAllocators<ClientConnection> &allocator() { return allocators_; }
    void Poll() override {
      Worker::Task t;
      while (queue_.try_dequeue(t)) { t(); }
      Loop::Poll();
    }
    void Close() override { transport_.Fin(); }
    void Enqueue(Worker::Task &&t) override { queue_.enqueue(std::move(t)); }
    base::Serial::PartitionId GetPartitionId() const override { return partition_id(); }
    void ResetPartition() override { Loop::ResetPartitionId(); }
    void Resolve(int family_pref, const std::string &host, qrpc_on_resolve_host_t cb) override {
      ASSERT(false);
    }
    qrpc_transport_type_t transport_type() const override { return QRPC_TRANSPORT_WEBRTC; }
  private:
    AsyncResolver resolver_;
    qrpc_clconf_t config_;
    base::webrtc::Client transport_;
    HandleAllocators<ClientConnection> allocators_;
    Worker::TaskQueue queue_;
  };
}
}
