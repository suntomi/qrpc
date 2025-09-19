#pragma once

#include <map>

#include "qrpc.h"

namespace qrpc {
class HandlerMap {
public:
  typedef qrpc_handler_entry_t HandlerEntry;
  typedef qrpc_handler_type_t HandlerType;
private:
  std::map<std::string, HandlerEntry> map_;
  qrpc_stream_router_t router_;
public:
  HandlerMap() : map_() { qrpc_closure_init_noop(router_, qrpc_stream_router_t); }
  inline bool AddEntry(const std::string &name, qrpc_stream_handler_t stream) {
    HandlerEntry he;
    he.type = STREAM;
    he.stream = stream;
    map_[name] = he;
    return true;
  }
  inline bool AddEntry(const std::string &name, qrpc_rpc_handler_t rpc) {
    HandlerEntry he;
    he.type = RPC;
    he.rpc = rpc;
    map_[name] = he;
    return true;
  }
  inline const HandlerEntry *Find(const std::string &name, qrpc_conn_t conn) const {
    if (!qrpc_closure_is_empty(router_)) {
      return qrpc_closure_call(router_, name.c_str(), conn);
    }
    auto it = map_.find(name);
    return it == map_.end() ? nullptr : &(it->second);
  }
  inline void SetRouter(qrpc_stream_router_t router) {
    router_ = router;
  }
  inline qrpc_hdmap_t ToHandle() { return (qrpc_hdmap_t)this; }
  static inline HandlerMap *FromHandle(qrpc_hdmap_t hdm) { return (HandlerMap *)hdm; }
  static inline HandlerMap &empty() {
    static HandlerMap empty;
    return empty;
  }
};
typedef HandlerMap::HandlerEntry HandlerEntry;
typedef HandlerMap::HandlerType HandlerType;
}
