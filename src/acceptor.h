#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "queue.h"
#include "plog.h"
#include "config.h"

#include <thread>
#include <memory>

namespace quarrel {
    class Acceptor {
        public:
            explicit Acceptor(std::shared_ptr<Configure> config);
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
            PlogMng pmn_;
            uint64_t term_; // logical time
            std::shared_ptr<Configure> config_;

            std::thread thread_;
            PaxosStateMachine state_;
            LockFreeQueue<std::unique_ptr<PaxosMsg>> msg_;
    };
}

#endif
