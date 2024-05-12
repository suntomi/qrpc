// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <map>
#include <tuple>
#include <mutex>
#include <condition_variable>

#include "qrpc.h"

#include "qrpc/base.h"
#include "qrpc/handler_map.h"
#include "qrpc/worker.h"

namespace qrpc {
// server is a class to manage multiple workers and ports.
class Server {
 public:
	typedef Worker::TaskQueue TaskQueue;
  struct PortConfig : public qrpc_svconf_t {
    HandlerMap handler_map;
    qrpc_addr_t addr;

    PortConfig(const qrpc_addr_t &a, const qrpc_svconf_t &config) : 
     qrpc_svconf_t(config), handler_map(), addr(a) {}
  }; 
  enum Status {
    RUNNING,
    TERMINATING,
    TERMINATED,
  };
 protected:
  std::atomic<Status> status_;
  uint32_t process_index_, n_worker_;  // process index in cluster (eg. statefulset number in k8s), number of worker
	std::unique_ptr<TaskQueue[]> worker_queue_;
	std::unordered_map<int, PortConfig> port_configs_;
  std::unordered_map<int, Worker*> workers_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::thread shutdown_thread_;
  IdFactory<uint32_t> stream_index_factory_;

 public:
	Server(uint32_t n_worker) : 
    status_(RUNNING), n_worker_(n_worker), worker_queue_(nullptr), 
    stream_index_factory_(0x7FFFFFFF) {}
  ~Server() {}
  HandlerMap *Open(const qrpc_addr_t &addr, const qrpc_svconf_t &conf) {
    if (port_configs_.find(addr.port) != port_configs_.end()) {
      return nullptr; //already port used
    } 
    auto pc = port_configs_.emplace(std::piecewise_construct, 
                std::forward_as_tuple(addr.port), std::forward_as_tuple(addr, conf));
    if (!pc.second) {
      ASSERT(false);
      return nullptr;
    }
    auto &pconf = pc.first->second;
    //first is iterator of map<int, PortConfig>
    return &(pconf.handler_map);
  }
	int Start(bool block) {
    if (!alive()) { return QRPC_OK; }
		worker_queue_.reset(new TaskQueue[n_worker_]);
		if (worker_queue_ == nullptr) {
			return QRPC_EALLOC;
		}
    int r = 0;
		for (uint32_t i = 0; i < n_worker_; i++) {
			if ((r = StartWorker(i)) < 0) {
				return r;
			}
		}
    if (block) {
      std::unique_lock<std::mutex> lock(mutex_);
      cond_.wait(lock, [this]() { return !alive(); });
      //TRACE("exit wait: mutex_ should locked");
      ASSERT(lock.owns_lock() && !alive());
      Stop();
      //TRACE("exit thread: mutex_ should unlocked");
    } else {
      shutdown_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]() { return !alive(); });
        ASSERT(lock.owns_lock() && !alive());
        Stop();
      });
    }
		return QRPC_OK;
	}
  void Join() {
    if (!alive()) { return; }
    {
      std::unique_lock<std::mutex> lock(mutex_); //wait for Stop() call finished
      status_ = TERMINATING;
    }
    cond_.notify_all();
    {
      //wait for Stop() call finished by wait for condition_variable.
      //note that mutex_ is not assured to be locked by the thread which is waken by above notify_all here.
      std::unique_lock<std::mutex> lock(mutex_); 
      //finailized should evaluated before first actual wait operation, so never deadlock.
      //if reach here earlier than Stop() finished, this thread should be waken up by the thread calls Stop(),
      //otherwise, Stop() is already finished here and terminated() returns true
      cond_.wait(lock, [this]() { return terminated(); });
      if (shutdown_thread_.joinable()) {
        shutdown_thread_.join(); //ensure shutdown thread finished
      }
    }
  }
  TaskQueue &queue(int idx) { return worker_queue_[idx]; }
  inline bool alive() const { return status_ == RUNNING; }
  inline bool terminated() const { return status_ == TERMINATED; }
  inline uint32_t n_worker() const { return n_worker_; }
  inline uint32_t process_index() const { return process_index_; }
  inline const std::unordered_map<int, PortConfig> &port_configs() const { return port_configs_; }
  inline std::unordered_map<int, PortConfig> &port_configs() { return port_configs_; }
  inline qrpc_server_t ToHandle() { return (qrpc_server_t)this; }
  inline IdFactory<uint32_t> &stream_index_factory() { return stream_index_factory_; }
  static inline Server *FromHandle(qrpc_server_t sv) { return (Server *)sv; }

 protected:
  void Stop() {
    for (auto &kv : workers_) {
      kv.second->Join();
    } 
    status_ = TERMINATED;
    cond_.notify_all();
  }
  int StartWorker(int index) {
    auto l = new Worker(index, *this);
    workers_[index] = l;
    l->Start(worker_queue_[index]);
    return QRPC_OK;
  } 
};
}