#ifndef __QUARREL_IDGEN_H_
#define __QUARREL_IDGEN_H_

#include "ptype.h"
#include <sys/time.h>

namespace quarrel {
class IdGen {
 public:
  IdGen(uint64_t start, uint64_t step) : id_(start), step_(step) {}

  void SetStartId(uint64_t id) { id_ = id; }
  void SetStep(uint64_t step) { step_ = step; }

  uint64_t Get() const { return id_; }
  uint64_t GetStep() const { return step_; }

  uint64_t GetAndInc() {
    uint64_t v = id_;
    id_ += step_;
    return v;
  }

  void Reset(uint64_t start) {
    id_ = start;
  }

  uint64_t SetGreaterThan(uint64_t v) {
    if (v <= id_) return id_;

    id_ += (v - id_ + step_ - 1) / step_ * step_;
    return id_;
  }

 private:
  uint64_t id_;
  uint64_t step_;
};

class IdGenByDate : public IdGen {
 public:
  IdGenByDate(uint64_t mask, uint64_t step) : IdGen(1, step) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    SetGreaterThan(tv.tv_sec & mask);
  }
};
}  // namespace quarrel

#endif
