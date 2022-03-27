#pragma once

#include <functional>
#include <vector>

#include "queue.h"

namespace quarrel {

template <typename T>
class FixCountVector {
 public:
  void Init(int capacity) {
    capacity_ = capacity;
    pool_.reserve(capacity);
  }

  uint64_t Size() {
    return pool_.size();
  }

  int Enqueue(const T& v) {
    if (pool_.size() == capacity_) return -1;

    pool_.push_back(v);
    return 0;
  }

  int Dequeue(T& v) {
    if (pool_.empty()) return -1;

    v = std::move(pool_.back());
    pool_.resize(pool_.size() - 1);
    return 0;
  }

 private:
  uint64_t capacity_;
  std::vector<T> pool_;
};

template <typename T>
class FixCountLockFreeQueue {
 public:
  void Init(int capacity) {
    pool_.Init(capacity);
  }

  uint32_t Size() {
    return pool_.Size();
  }

  int Enqueue(T v) {
    return pool_.Enqueue(std::move(v), false);
  }

  int Dequeue(T& v) {
    return pool_.Dequeue(v, false);
  }

 private:
  LockFreeQueueV2<T> pool_;
};

template <typename T, typename C>
class FixCountAllocator {
 public:
  using Allocator = std::function<T()>;
  using Releaser = std::function<void(T)>;

  FixCountAllocator() {}

  void Init(int shard, uint32_t capacity, Allocator alloc, Releaser release) {
    capacity_ = capacity;

    pool_.resize(shard);
    for (auto& q : pool_) {
      q.Init(capacity);
    }
    alloc_ = std::move(alloc);
    release_ = std::move(release);
  }

  ~FixCountAllocator() {
    for (auto& q : pool_) {
      while (q.Size() > 0) {
        T v;
        if (q.Dequeue(v)) {
          break;
        }
        release_(std::move(v));
      }
    }
    pool_.clear();
  }

  T AllocByShard(int shard) {
    auto& q = pool_[shard % pool_.size()];

    T v;
    int ret = q.Dequeue(v);
    if (ret) {
      v = alloc_();
    }

    return v;
  }

  void ReleaseByShard(int shard, T v) {
    auto& q = pool_[shard % pool_.size()];
    if (q.Size() >= capacity_) {
      release_(std::move(v));
    } else {
      auto ret = q.Enqueue(v);
      if (ret) {
        release_(std::move(v));
      }
    }
  }

 private:
  Allocator alloc_;
  Releaser release_;

  uint32_t capacity_;
  std::vector<C> pool_;
};

template <typename T>
using FixCountVectorAllocator = FixCountAllocator<T, FixCountVector<T>>;

template <typename T>
using FixCountLockFreeAllocator = FixCountAllocator<T, FixCountLockFreeQueue<T>>;

}  // namespace quarrel
