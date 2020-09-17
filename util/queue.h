#pragma once

#include <stdint.h>
#include <string.h>
#include <memory>

namespace quarrel {

template <class T>
class LockFreeQueue {
 public:
  enum {
    RT_OK = 0,     //成功
    RT_FULL = 1,   //满了
    RT_EMPTY = 2,  //没数据
    RT_ERR = -1,   //出错
  };

  enum {
    FLAG_NULL = 0,
    FLAG_SETTOOK = 1,
    FLAG_SETED = 2,
    FLAG_GETTOOK = 3,
  };

  LockFreeQueue() : init_(false), head_(nullptr) {}

  ~LockFreeQueue() {
    if (head_) {
      char* p = reinterpret_cast<char*>(head_);
      delete[] p;
      p = nullptr;
    }
  }

  int Init(const uint32_t size) {
    if (init_) {
      return RT_OK;
    }

    uint64_t totalSize = sizeof(Head) + (size + 1) * sizeof(Entry);

    head_ = reinterpret_cast<Head*>(new char[totalSize]);

    memset(head_, 0, totalSize);

    head_->head = head_->tail = 0;

    head_->size = size;

    init_ = true;

    return RT_OK;
  }

  int Enqueue(const T& val, bool block) {
    if (!init_) {
      return RT_ERR;
    }

    bool success = false;
    uint64_t old_head = 0;
    uint64_t old_tail = 0;

    do {
      old_head = head_->head;
      old_tail = head_->tail;

      __sync_synchronize();

      if (old_head >= old_tail) {
        if (head_->size > (old_head - old_tail)) {
          success = __sync_bool_compare_and_swap(&head_->head, old_head,
                                                 old_head + 1);
        } else {
          if (block) {
            continue;
          } else {
            return RT_FULL;
          }
        }
      } else {
        uint64_t new_tail = old_tail % head_->size;
        uint64_t new_head = static_cast<uint64_t>(0xffffffffffffffff) -
                            old_tail + old_head + 1 + new_tail;
        if (__sync_bool_compare_and_swap(&head_->head, old_head, new_head)) {
          while (
              !__sync_bool_compare_and_swap(&head_->tail, old_tail, new_tail))
            ;
        }
      }
    } while (!success);

    uint64_t pos = old_head % head_->size;
    do {
      success = __sync_bool_compare_and_swap(
          &((reinterpret_cast<Entry*>(&(head_[1])))[pos].flag), FLAG_NULL,
          FLAG_SETTOOK);
    } while (!success);

    (reinterpret_cast<Entry*>(&(head_[1])))[pos].data = val;

    __sync_synchronize();

    (reinterpret_cast<Entry*>(&(head_[1])))[pos].flag = FLAG_SETED;

    return RT_OK;
  }

  int Dequeue(T& val, bool block) {
    if (!init_) {
      return RT_ERR;
    }

    bool success = false;
    uint64_t old_head = 0;
    uint64_t old_tail = 0;

    do {
      old_head = head_->head;
      old_tail = head_->tail;

      __sync_synchronize();

      if (old_tail < old_head) {
        success =
            __sync_bool_compare_and_swap(&head_->tail, old_tail, old_tail + 1);
      } else {
        if (block) {
          continue;
        } else {
          return RT_EMPTY;
        }
      }
    } while (!success);

    uint64_t pos = old_tail % head_->size;

    do {
      success = __sync_bool_compare_and_swap(
          &((reinterpret_cast<Entry*>(&(head_[1])))[pos].flag), FLAG_SETED,
          FLAG_GETTOOK);
    } while (!success);

    val = std::move((reinterpret_cast<Entry*>(&(head_[1])))[pos].data);

    __sync_synchronize();

    (reinterpret_cast<Entry*>(&(head_[1])))[pos].flag = FLAG_NULL;

    return RT_OK;
  }

  unsigned int Size() {
    uint64_t old_head = head_->head;
    uint64_t old_tail = head_->tail;

    __sync_synchronize();

    if (old_head > old_tail) {
      return old_head - old_tail;
    }

    return 0;
  }

 protected:
  struct Entry {
    volatile uint8_t flag;
    T data;
  };

  struct Head {
    volatile uint64_t head;
    volatile uint64_t tail;
    uint32_t size;
  };

  bool init_;
  Head* head_;

} __attribute__((aligned(64)));

}  // namespace quarrel
