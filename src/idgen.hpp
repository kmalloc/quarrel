#ifndef __QUARREL_IDGEN_H_
#define __QUARREL_IDGEN_H_

#include "ptype.h"

#include <atomic>

namespace quarrel {
    class IdGen {
        public:
            IdGen(uint64_t start, uint64_t step):
                step_(step), id_(start) {}

            void SetStep(uint64_t step) { step_ = step; }
            uint64_t GetStep() const { return step_; }
            uint64_t GetAndInc() { uint64_t v = id_; id_ += step_; return v; }

            uint64_t SetGreatThan(int v) { id_ += (v - id_ + step_ - 1)/step_ * step_;  return id_; }

        private:
            uint64_t step_;
            std::atomic<uint64_t> id_;
    };
}

#endif
