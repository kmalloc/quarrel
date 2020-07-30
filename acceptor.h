#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "queue.h"
#include "entry.h"

#include <functional>

namespace quarrel {
    class Acceptor {
        public:
            explicit Acceptor(int id);
            ~Acceptor();

            int Prepare(const Proposal& proposal);
            int Accept(const Proposal& proposal);

        private:
            Acceptor(const Acceptor&) = delete;
            Acceptor& operator=(const Acceptor&) = delete;

        private:
            uint64_t term_;

            uint64_t local_chosen_entry_;
            uint64_t global_chosen_entry_;

            PaxosStateMachine state_;
    };
};

#endif
