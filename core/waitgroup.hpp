#ifndef __QUARREL_WAITGROUP_H_
#define __QUARREL_WAITGROUP_H_

#include <mutex>
#include <chrono>
#include <condition_variable>

namespace quarrel {

class WaitGroup {
 public:
  WaitGroup() : curr_(0), count_(0) {}
  explicit WaitGroup(uint32_t count) : curr_(0), count_(count) {}

  void Notify() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    ++curr_;
    if (curr_ >= count_) {
      condition_.notify_one();
    }
  }

  bool Wait(uint32_t timeout_ms) {
    auto timeout = std::chrono::milliseconds(timeout_ms);
    std::unique_lock<decltype(mutex_)> lock(mutex_);

    while (count_ > curr_) {
      // Handle spurious wake-ups, timeout value should re-adjust
      auto r = condition_.wait_for(lock, timeout);
      if (curr_ >= count_) break;

      if (r == std::cv_status::timeout) return false;
    }

    curr_ = 0;
    return true;
  }

  bool TryWait() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    bool ret = (curr_ >= count_);
    if (ret) curr_ = 0;

    return ret;
  }

  void Reset(uint32_t count) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    curr_ = 0;
    count_ = count;
  }

 private:
  uint32_t curr_;
  uint32_t count_;
  uint32_t timeout_ms_;

  std::mutex mutex_;
  std::condition_variable condition_;
};

}  // namespace quarrel

#endif
