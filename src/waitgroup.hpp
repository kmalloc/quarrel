#ifndef __QUARREL_WAITGROUP_H_
#define __QUARREL_WAITGROUP_H_

#include <mutex>
#include <chrono>
#include <condition_variable>

namespace quarrel {

class WaitGroup {
 public:
  WaitGroup(uint32_t count, uint32_t timeout_ms): curr_(0), count_(count), timeout_ms_(timeout_ms) {}

  void Notify() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    ++curr_;
    if (curr_ >= count_) {
        condition_.notify_one();
    }
  }

  bool Wait() {
    auto timeout = std::chrono::milliseconds(timeout_ms_);
    std::unique_lock<decltype(mutex_)> lock(mutex_);

    while (true) { // Handle spurious wake-ups, timeout value should re-adjust
      auto r = condition_.wait_for(lock, timeout);
      if (curr_ >= count_) break;

      if (r == std::cv_status::timeout) return false;
    }

    return true;
  }

  bool TryWait() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    return curr_ >= count_;
  }

 private:
  uint32_t curr_;
  uint32_t count_;
  uint32_t timeout_ms_;

  std::mutex mutex_;
  std::condition_variable condition_;
};

} // namespace quarrel

#endif
