#ifndef __QUARREL_IDGEN_H_
#define __QUARREL_IDGEN_H_

#include "ptype.h"

#include <atomic>

namespace quarrel {
    class IdGen {
        public:
            IdGen(uint64_t start, uint64_t step):
                id_(start), step_(step) {}

            void SetID(uint64_t val) { id_ = val; }
            void SetStep(uint64_t step) { step_ = step; }
            uint64_t GetStep() const { return step_; }
            uint64_t GetAndInc() { id_ += step_; return id_; }

        private:
            uint64_t step_;
            std::atomic<uint64_t> id_;
    };
}

#endif
