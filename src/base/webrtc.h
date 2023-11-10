#pragma once

#include "base/session.h"

#include "RTC/IceCandidate.hpp"

namespace base {
  class WebRTCServer {
  public:
    typedef RTC::IceCandidate IceCandidate;
    class IceUserName : public std::string {};
  public: // sessions
    class TcpSession : public TcpListener::TcpSession {
    public:
      TcpSession(SessionFactory &f, Fd fd, const Address &addr) : 
        TcpListener::TcpSession(f, fd, addr) {}
        int OnRead(const char *p, size_t sz) override {
          return QRPC_OK;
        }
    };
    class UdpSession : public UdpListener::UdpSession {
    public:
      UdpSession(SessionFactory &f, Fd fd, const Address &addr) : 
        UdpListener::UdpSession(f, fd, addr) {}
      int OnRead(const char *p, size_t sz) override {
          return QRPC_OK;        
      }
    };
  public: // servers
    typedef TcpServer<TcpSession> BaseTcpServer;
    typedef UdpServer<UdpSession> BaseUdpServer;
    class TcpPort : public BaseTcpServer {
    public:
        TcpPort(Loop &l) : BaseTcpServer(l) {}
    };
    class UdpPort : public BaseUdpServer {
    public:
        UdpPort(Loop &l) : BaseUdpServer(l) {}
    };
  public: // connections
    class Connection {
    public:
      Connection(WebRTCServer &sv) : sessions_(), sv_(sv), last_active_(0) {}
      ~Connection() {}
    protected:
      qrpc_time_t last_active_;
      std::vector<Session*> sessions_;
      WebRTCServer &sv_;
    };
    struct Config {
      enum Protocol {
        NONE = 0,
        UDP = 1,
        TCP = 2,
      };
      Protocol protocol;
      std::string ip;
      uint32_t priority;
    };
  public:
    WebRTCServer(Loop &l, std::vector<Config> &&configs) : configs_(configs) {}
    int Init();
    int Fin();
  protected:
    std::vector<Config> configs_;
    std::vector<TcpPort> tcp_ports_;
    std::vector<UdpPort> udp_ports_;
    std::map<IceUserName, Connection> connections_;
  private:
    static int GlobalInit();
    static void GlobalFin();
  };
} //namespace base