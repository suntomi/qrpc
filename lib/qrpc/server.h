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
#include "qrpc/worker.h"

namespace qrpc {
// server is a class to manage multiple workers and ports.
class Server {
public:
	typedef Worker::TaskQueue TaskQueue;
  typedef base::Serial::PartitionId PartitionId;
  typedef qrpc_listen_conf_t PortConfig;
  enum Status {
    RUNNING,
    TERMINATING,
    TERMINATED,
  };
protected:
  std::atomic<Status> status_;
  uint32_t process_index_, n_worker_, max_nfd_;  // process index in cluster (eg. statefulset number in k8s), number of worker
	std::unique_ptr<TaskQueue[]> worker_queue_;
	std::unordered_map<int, PortConfig> port_configs_;
  std::unordered_map<int, Worker*> workers_;
  PartitionId start_partition_id_{0};
  std::mutex mutex_;
  std::condition_variable cond_;
  std::thread shutdown_thread_;
  static thread_local std::mutex g_mutex_;
public:
	Server(const qrpc_svconf_t &conf) : status_(RUNNING),
    n_worker_(conf.n_worker), max_nfd_(conf.max_nfd),
    process_index_(conf.process_index), worker_queue_(nullptr) {}
  ~Server() = default;
  int Open(const qrpc_listen_conf_t &conf) {
    if (port_configs_.find(conf.ep.port) != port_configs_.end()) {
      QRPC_LOGJ(error, {{"ev","port dup"},{"port",conf.ep.port}});
      return QRPC_EDUP; //already port used
    } 
    auto pc = port_configs_.emplace(std::piecewise_construct, 
                std::forward_as_tuple(conf.ep.port), std::forward_as_tuple(conf));
    if (!pc.second) {
      QRPC_LOGJ(error, {{"ev","port dup"},{"port",conf.ep.port}});
      ASSERT(false);
      return QRPC_EDUP;
    }
    return QRPC_OK;
  }
	int Start(bool block) {
    if (!alive()) { return QRPC_OK; }
		worker_queue_.reset(new TaskQueue[n_worker_]);
		if (worker_queue_ == nullptr) {
			return QRPC_EALLOC;
		}
    int r = StartWorkers();
    if (r < 0) {
      return r;
    }
    auto shutdown_block = [this]() {
      std::unique_lock<std::mutex> lock(mutex_);
      cond_.wait(lock, [this]() { return !alive(); });
      //TRACE("exit wait: mutex_ should locked");
      ASSERT(lock.owns_lock() && !alive());
      //TRACE("exit thread: mutex_ should unlocked");
      Stop();
    };
    if (block) {
      shutdown_block();
    } else {
      shutdown_thread_ = std::thread(shutdown_block);
    }
		return QRPC_OK;
	}
  TaskQueue &AddWorker(base::Serial::PartitionId id, Worker *w) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto idx = id - start_partition_id_;
    auto it = workers_.find(idx);
    if (it != workers_.end()) {
      logger::die({{"ev","Server::AddWorker(): worker already exists"},{"index",idx}});
    }
    workers_.emplace(idx, w);
    return worker_queue_[idx];
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
  TaskQueue &queue(PartitionId id) {
    ASSERT(OwnsPartitionId(id));
    return worker_queue_[id - start_partition_id_];
  }
  inline bool alive() const { return status_ == RUNNING; }
  inline bool terminated() const { return status_ == TERMINATED; }
  inline uint32_t n_worker() const { return n_worker_; }
  inline uint32_t max_nfd() const { return max_nfd_; }
  inline uint32_t max_nfd_per_worker() const { return std::floor(max_nfd_ / n_worker_); }
  inline uint32_t process_index() const { return process_index_; }
  inline PartitionId start_partition_id() const { return start_partition_id_; }
  inline bool OwnsPartitionId(PartitionId id) const {
    return start_partition_id_ != 0 && id >= start_partition_id_ && id < (start_partition_id_ + n_worker_);
  }
  qrpc_transport_type_t transport_type() const;
  inline const std::unordered_map<int, PortConfig> &port_configs() const { return port_configs_; }
  inline std::unordered_map<int, PortConfig> &port_configs() { return port_configs_; }
  inline qrpc_server_t ToHandle() { return reinterpret_cast<qrpc_server_t>(this); }
  static inline Server *FromHandle(qrpc_server_t sv) { return reinterpret_cast<Server *>(sv); }

protected:
  void Stop() {
    for (auto &kv : workers_) {
      kv.second->Join();
    } 
    status_ = TERMINATED;
    cond_.notify_all();
  }
  int StartWorkers();
  int StartWorker(PartitionId partition_id) {
    auto w = new Worker();
    w->Start(partition_id, *this);
    return QRPC_OK;
  } 
};
}
