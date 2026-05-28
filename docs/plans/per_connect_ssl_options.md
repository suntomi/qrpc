# Per-Connect SSL Options Plan

## Background

Currently, `base::SessionFactory::Connect` decides whether SSL/TLS is used based on the `MaybeCertPair` stored in the factory config.

This causes two problems:

1. SSL usage for client connections is fixed at factory construction time, even though it should be selectable per `Connect` call.
2. `base::SessionFactory` mixes client and listener responsibilities. In particular, listener-only concerns such as server certificate ownership affect client-side connection behavior.


## Goals

1. Make SSL/TLS enablement for client connections configurable per `Connect` call.
2. Prevent listener-side factory types from exposing `Connect`.
3. Separate client responsibilities from listener responsibilities in the type hierarchy.
4. Keep the change incremental and avoid a large API break where possible.


## High-Level Design

### 1. Split `SessionFactory` into common, client, and listener roles

Refactor the base classes as follows:

```cpp
SessionFactory
├─ ClientSessionFactory
└─ ListenerSessionFactory
```

`SessionFactory` becomes the minimal common base that owns:

- `Loop`
- `AlarmProcessor`
- `FactoryMethod`
- session timeout
- session lifetime / timeout helpers

`ClientSessionFactory` owns:

- `Resolver`
- `Connect(...)`
- client-side TLS context
- per-connect option injection

`ListenerSessionFactory` owns:

- listener-side TLS configuration
- server certificate ownership
- server-side TLS context


### 2. Make TCP/UDP session factories role-specific by type

TCP and UDP factories should share implementation through templates, but expose role-specific names:

```cpp
template <class RoleBase>
class TcpSessionFactoryT;

template <class RoleBase>
class UdpSessionFactoryT;

using TcpClientSessionFactory = TcpSessionFactoryT<ClientSessionFactory>;
using TcpListenerSessionFactory = TcpSessionFactoryT<ListenerSessionFactory>;

using UdpClientSessionFactory = UdpSessionFactoryT<ClientSessionFactory>;
using UdpListenerSessionFactory = UdpSessionFactoryT<ListenerSessionFactory>;
```

Then keep the concrete public types:

```cpp
class TcpClient : public TcpClientSessionFactory;
class TcpListener : public TcpListenerSessionFactory, public IoProcessor;

class UdpClient : public UdpClientSessionFactory;
class UdpListener : public UdpListenerSessionFactory, public IoProcessor;
```

This ensures:

- `Connect(...)` exists only on client-side factories
- listener-side types cannot call `Connect(...)`
- TCP/UDP session management code remains shared


### 3. Move `Resolver` out of `SessionFactory`

`Resolver` is only needed for `Connect(...)`, so it should be removed from `SessionFactory::Config` and `SessionFactory`.

Proposed config split:

```cpp
class SessionFactory {
 public:
  struct Config {
    qrpc_time_t session_timeout;
  };
};

class ClientSessionFactory : public SessionFactory {
 public:
  struct Config : public SessionFactory::Config {
    Resolver &resolver;
  };
};

class ListenerSessionFactory : public SessionFactory {
 public:
  struct Config : public SessionFactory::Config {};
};
```


## Per-Connect SSL/TLS Configuration

### 4. Introduce `ClientSessionFactory::ConnectOptions`

Client-side connection options should be passed to `Connect(...)`:

```cpp
struct ConnectOptions {
  bool use_tls{false};
};
```

Initial scope:

- `use_tls`

Possible later extensions:

- `server_name`
- `verify_peer`
- CA file/path

`ClientSessionFactory::Connect(...)` will accept `ConnectOptions` and apply them to the created session.


### 5. Pass `ConnectOptions` to sessions by wrapping `FactoryMethod`

We will keep the current `FactoryMethod` signature:

```cpp
using FactoryMethod = std::function<Session *(Fd, const Address &)>;
```

Instead of changing that signature, `ClientSessionFactory::Connect(...)` will wrap the user-provided `FactoryMethod` and inject `ConnectOptions` into the created session.

Conceptually:

```cpp
FactoryMethod wrapped = [m, opts](Fd fd, const Address &addr) -> SessionFactory::Session * {
  auto *s = m(fd, addr);
  auto *cs = dynamic_cast<ClientSessionFactory::Session *>(s);
  if (cs == nullptr) {
    logger::die(...);
  }
  cs->ApplyConnectOptions(opts);
  return s;
};
```

This keeps the change localized and avoids changing every factory callback signature in the codebase.


## Session Role and TLS Query API

### 6. Add virtual query functions to `SessionFactory::Session`

Instead of introducing a single `TransportMode` enum, session state should expose two orthogonal attributes:

- whether the session belongs to a listener or a client
- whether TLS is enabled for that session

Add virtual functions to `SessionFactory::Session`:

```cpp
virtual bool is_listener() const { return false; }
virtual bool need_tls() const { return false; }
virtual SSL_CTX *tls_ctx() const { return nullptr; }
```


### 7. Introduce role-specific session base classes

Each role base owns its own session subclass:

```cpp
class ClientSessionFactory : public SessionFactory {
 public:
  class Session : public SessionFactory::Session {
   public:
    bool is_listener() const override { return false; }
    bool need_tls() const override { return use_tls_; }
    SSL_CTX *tls_ctx() const override;
    void ApplyConnectOptions(const ConnectOptions &opts);

   private:
    bool use_tls_{false};
  };
};
```

```cpp
class ListenerSessionFactory : public SessionFactory {
 public:
  class Session : public SessionFactory::Session {
   public:
    bool is_listener() const override { return true; }
    bool need_tls() const override;
    SSL_CTX *tls_ctx() const override;
  };
};
```

TCP and UDP session implementations should inherit from `RoleBase::Session`, not directly from `SessionFactory::Session`.

That means:

- `TcpClientSessionFactory::TcpSession` inherits from `ClientSessionFactory::Session`
- `TcpListenerSessionFactory::TcpSession` inherits from `ListenerSessionFactory::Session`
- same for UDP


## TLS Ownership Split

### 8. Listener-side TLS remains factory-scoped

Listener TLS should remain fixed at factory construction time.

`ListenerSessionFactory::Config` should own:

```cpp
MaybeCertPair certpair;
```

`ListenerSessionFactory` should initialize and own:

- `MaybeCertPair certpair_`
- `SSL_CTX *tls_ctx_`

Listener session `need_tls()` returns whether a valid listener TLS configuration exists.


### 9. Client-side TLS becomes connect-scoped

Client TLS enablement should no longer depend on `MaybeCertPair`.

Instead:

- client factory owns a reusable client-side `SSL_CTX`
- each connect attempt decides whether TLS is enabled by `ConnectOptions::use_tls`

Client session `need_tls()` returns the per-connect value stored by `ApplyConnectOptions(...)`.


## Handshaker Changes

### 10. `Handshaker` should query session state only

`Handshaker` should stop relying on factory-level `need_tls()` or `is_listener()`.

Instead:

- `Handshaker::Create(session)` checks `session.need_tls()`
- TLS handshake direction is selected by `session.is_listener()`
- `TlsHandshaker` obtains the context through `session.tls_ctx()`

Conceptually:

```cpp
Handshaker *Handshaker::Create(Session &s) {
  return s.need_tls() ? new TlsHandshaker(s) : new PlainHandshaker(s);
}
```

```cpp
int r = s.is_listener() ? SSL_accept(ssl_) : SSL_connect(ssl_);
```

```cpp
ssl_ = SSL_new(s.tls_ctx());
```

This removes the handshake logic's dependency on factory role/type details.


## Planned Refactoring Steps

1. Introduce `ClientSessionFactory` and `ListenerSessionFactory`.
2. Move `Resolver` from `SessionFactory` into `ClientSessionFactory`.
3. Move `SessionFactory::Connect(...)` into `ClientSessionFactory`.
4. Add `ClientSessionFactory::ConnectOptions`.
5. Wrap `FactoryMethod` in `ClientSessionFactory::Connect(...)` and inject per-connect options into sessions.
6. Introduce `ClientSessionFactory::Session` and `ListenerSessionFactory::Session`.
7. Add `Session::is_listener()`, `Session::need_tls()`, and `Session::tls_ctx()`.
8. Change TCP/UDP session implementations to inherit from `RoleBase::Session`.
9. Template TCP/UDP session factories as `TcpSessionFactoryT<RoleBase>` and `UdpSessionFactoryT<RoleBase>`.
10. Expose `TcpClientSessionFactory`, `TcpListenerSessionFactory`, `UdpClientSessionFactory`, and `UdpListenerSessionFactory` as aliases.
11. Move listener-side certificate ownership and TLS context into `ListenerSessionFactory`.
12. Add a reusable client-side TLS context to `ClientSessionFactory`.
13. Update `Handshaker` to query session state instead of factory state.
14. Remove obsolete factory-level TLS and listener/client role APIs from `SessionFactory`.


## Expected Outcome

After this refactoring:

- TLS on client connections can be selected per `Connect(...)`
- listener types do not expose `Connect(...)`
- client-only dependencies such as `Resolver` stay on client-side classes
- listener-only dependencies such as server certificates stay on listener-side classes
- handshake logic becomes session-driven instead of factory-driven


## Notes

- The initial `ConnectOptions` can remain minimal and contain only `use_tls`.
- The chosen approach intentionally avoids changing `FactoryMethod` signature across the codebase.
- Listener-side TLS remains factory-scoped by design, while client-side TLS becomes connect-scoped.
