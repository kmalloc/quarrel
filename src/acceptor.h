#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "queue.h"
#include "plog.h"
#include "conn.h"
#include "config.h"
#include "waitgroup.hpp"

#include <atomic>
#include <thread>
#include <memory>
#include <vector>

namespace quarrel {

    struct PaxosRequest {
      ResponseCallback cb_;
      std::shared_ptr<PaxosMsg> msg_;
    };

    struct WorkerData {
        WaitGroup wg_;
        std::thread th_;
        LockFreeQueue<PaxosRequest> mq_;
        std::atomic<uint64_t> pending_{0};
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

            // note: every request required a response.
            int AddMsg(std::shared_ptr<PaxosMsg> m, ResponseCallback cb);

            void SetPlogMng(std::shared_ptr<PlogMng> pm) {
                pmn_ = std::move(pm);
            }

            void SetConfig(std::shared_ptr<Configure> config) {
                config_ = std::move(config);
            }

        private:
            Acceptor(const Acceptor&) = delete;
            Acceptor& operator=(const Acceptor&) = delete;

            std::shared_ptr<PaxosMsg> HandleAcceptReq(const Proposal& proposal);
            std::shared_ptr<PaxosMsg> HandlePrepareReq(const Proposal& proposal);
            std::shared_ptr<PaxosMsg> HandleChosenReq(const Proposal& proposal);

            int WorkerProc(int workerid);
            int DoHandleMsg(PaxosRequest req);

        private:
            uint64_t term_; // logical time
            bool started_{false};
            std::atomic<uint8_t> run_{0};

            std::shared_ptr<ConnMng> conn_;
            std::shared_ptr<PlogMng> pmn_;
            std::shared_ptr<Configure> config_;

            std::vector<std::unique_ptr<WorkerData>> workers_;
    };
}

#endif
