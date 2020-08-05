#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "queue.h"
#include "plog.h"
#include "config.h"

#include <thread>
#include <memory>
#include <vector>

namespace quarrel {
    class Acceptor {
        public:
            explicit Acceptor(std::shared_ptr<Configure> config);
            ~Acceptor();

            // An acceptor maintains several worker threads,
            // each thread waits on a msg queue designated to a plog instance,
            // thread count must <= plog instance count,
            // ensuring that each plog instance is mutated from one thread only.
            int StartWorker();
            int StopWorker();
            int AddMsg(std::unique_ptr<PaxosMsg> msg);

        private:
            Acceptor(const Acceptor&) = delete;
            Acceptor& operator=(const Acceptor&) = delete;

            int Accept(const Proposal& proposal);
            int Prepare(const Proposal& proposal);
            int HandleMsg(std::unique_ptr<PaxosMsg> msg);

        private:
            uint64_t term_; // logical time
            std::shared_ptr<PlogMng> pmn_;
            std::shared_ptr<Configure> config_;

            std::vector<std::thread> thread_;
            std::vector<LockFreeQueue<std::unique_ptr<PaxosMsg>>> msg_;
    };
}

#endif
