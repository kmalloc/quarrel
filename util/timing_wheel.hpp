#include <list>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <functional>

#include <sys/prctl.h>

namespace quarrel {

using TimingWheelNotifier = std::function<void(uint64_t)>;

class TimingWheel {
 public:
  explicit TimingWheel(int granularity_ms) : granularity_ms_(granularity_ms), wheel_(4000 / granularity_ms) {
  }

  ~TimingWheel() { Stop(); }

  void Stop() {
    if (state_ == 2) {
      return;
    }

    std::lock_guard<std::mutex> l(lock_);
    state_ = 2;
    worker_.join();
  }

  void Start() {
    std::lock_guard<std::mutex> l(lock_);
    if (state_ == 0) {
      state_ = 1;
      worker_ = std::thread(&TimingWheel::worker, this);
    }
  }

  bool AddTimeout(uint64_t opaque, uint32_t timeout_ms, TimingWheelNotifier notifier) {
    auto idx = timeout_ms / granularity_ms_ + (timeout_ms % granularity_ms_ ? 1 : 0);
    {
      std::lock_guard<std::mutex> l(lock_);
      idx = (idx + start_) % (int)wheel_.size();
      wheel_[idx].emplace_back(opaque, std::move(notifier));
    }

    return true;
  }

 private:
  struct Item {
    uint64_t opaque_;
    TimingWheelNotifier notifier_;

    Item(uint64_t opaque, TimingWheelNotifier n) : opaque_(opaque), notifier_(std::move(n)) {}
  };

  // disable copy
  TimingWheel(const TimingWheel&) = delete;
  TimingWheel& operator=(const TimingWheel&) = delete;

  void worker() {
    prctl(PR_SET_NAME, "timingwheel", 0, 0, 0);
    while (state_ != 2) {
      {
        std::lock_guard<std::mutex> l(lock_);

        auto& ls = wheel_[start_];
        for (auto& it : ls) {
          it.notifier_(it.opaque_);
        }

        ls.clear();
        start_ = (start_ + 1) % (int)wheel_.size();
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(granularity_ms_));
    }
  }

  std::mutex lock_;
  std::thread worker_;

  uint32_t start_{0};
  int granularity_ms_;
  std::atomic<uint32_t> state_{0};  // 0 inited 1 started 2 stop
  std::vector<std::list<Item>> wheel_;
};

}  // namespace quarrel
