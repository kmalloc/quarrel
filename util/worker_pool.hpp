#ifndef __QUARREL_WORKER_POOL_H_
#define __QUARREL_WORKER_POOL_H_

#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <functional>

#include <sys/prctl.h>

#include "queue.h"
#include "waitgroup.hpp"

namespace quarrel {

template <typename DATA>
class WorkerPool {
  using WorkerHandler = std::function<int(DATA)>;

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

 private:
  // will be indexed by thread num, make sure it is 64 byte aligned, so that no
  // false sharing will occur.
  struct WorkerData {
    WaitGroup wg_;
    std::thread th_;
    LockFreeQueue<DATA> mq_;
    std::atomic<uint64_t> pending_;
  };

  WorkerHandler handler_;
  std::atomic<int> status_{0};
  std::vector<WorkerData> workers_;

 public:
  explicit WorkerPool(WorkerHandler handler) : handler_(std::move(handler)) {}

  ~WorkerPool() {
    StopWorker();
  }

  void StopWorker() {
    if (status_.load() != 1) {
      return;
    }

    status_ = 2;

    for (auto& w : workers_) {
      w.wg_.Notify();
      w.th_.join();
    }
  }

  void StartWorker(int worker_count, int queue_sz) {
    if (status_ == 1) return;

    status_.store(1, std::memory_order_release);
    workers_ = std::vector<WorkerData>(worker_count);

    auto idx = 0;
    for (auto& w : workers_) {
      w.wg_.Reset(1);
      w.pending_ = 0;
      w.mq_.Init(queue_sz);
      w.th_ = std::thread(&WorkerPool<DATA>::workerProc, this, idx++);
    }
  }

  int AddWork(uint64_t wid, DATA data) {
    if (status_.load(std::memory_order_acquire) != 1) return -1;

    wid = wid % workers_.size();
    auto& w = workers_[wid];

    auto ret = w.mq_.Enqueue(std::move(data), false);
    if (ret) {
      return -2;
    }

    if (w.pending_.fetch_add(1) == 0) {
      w.wg_.Notify();
    }

    return 0;
  }

 private:
  void workerProc(int wid) {
    auto w = &workers_[wid];
    using QueueType = LockFreeQueue<DATA>;

    {
      char buff[256];
      snprintf(buff, sizeof(buff), "wpool-%d", wid);
      prctl(PR_SET_NAME, buff, 0, 0, 0);
    }

    while (status_ == 1) {
      DATA req;
      if (w->mq_.Dequeue(req, false) == QueueType::RT_EMPTY) {
        w->wg_.WaitUs(1);
        continue;
      }

      handler_(std::move(req));
      w->pending_.fetch_sub(1);
    }
  }
};

}  // namespace quarrel

#endif
