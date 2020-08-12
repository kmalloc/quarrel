#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "queue.h"
#include "plog.h"
#include "config.h"
#include "waitgroup.hpp"

#include <atomic>
#include <thread>
#include <memory>
#include <vector>

namespace quarrel {
    struct WorkerData {
        WaitGroup wg_;
        std::thread th_;
        std::atomic<uint64_t> pending_{0};
        LockFreeQueue<std::shared_ptr<PaxosMsg>> mq_;
    };

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
            int AddMsg(std::shared_ptr<PaxosMsg> msg);

            void SetPlogMng(std::shared_ptr<PlogMng> pm) {
                pmn_ = std::move(pm);
            }

            void SetConfig(std::shared_ptr<Configure> config) {
                config_ = std::move(config);
            }

        private:
            Acceptor(const Acceptor&) = delete;
            Acceptor& operator=(const Acceptor&) = delete;

            int Accept(const Proposal& proposal);
            int Prepare(const Proposal& proposal);

            int WorkerProc(int workerid);
            int DoHandleMsg(std::shared_ptr<PaxosMsg> msg);

        private:
            uint64_t term_; // logical time
            bool started_{false};
            std::atomic<uint8_t> run_{0};

            std::shared_ptr<PlogMng> pmn_;
            std::shared_ptr<Configure> config_;

            std::vector<std::unique_ptr<WorkerData>> workers_;
    };
}

#endif
