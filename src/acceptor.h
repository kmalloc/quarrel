#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "queue.h"
#include "entry.h"

#include <thread>
#include <functional>

namespace quarrel {
    class Acceptor {
        public:
            explicit Acceptor(int id);
            ~Acceptor();

            int StartWorker();
            int AddMsg(std::unique_ptr<PaxosMsg> msg);

        private:
            Acceptor(const Acceptor&) = delete;
            Acceptor& operator=(const Acceptor&) = delete;

            int Accept(const Proposal& proposal);
            int Prepare(const Proposal& proposal);
            int HandleMsg(std::unique_ptr<PaxosMsg> msg);

        private:
            // logical time
            uint64_t term_;

            uint64_t local_chosen_entry_;
            uint64_t global_chosen_entry_;

            std::thread thread_;
            PaxosStateMachine state_;
            LockFreeQueue<std::unique_ptr<PaxosMsg>> msg_;
    };
};

#endif
