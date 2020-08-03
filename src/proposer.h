#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "conn.h"
#include "plog.h"
#include "config.h"
#include "ptype.h"
#include "idgen.hpp"
#include "waitgroup.hpp"

#include <vector>
#include <memory>

namespace quarrel {

struct BatchRpcContext {
    int ret_;
    WaitGroup wg_;
    std::atomic<int> rsp_count_;
    std::shared_ptr<PaxosMsg> rsp_msg_[MAX_ACCEPTOR_NUM];

    BatchRpcContext(int expect_rsp_count): wg_(expect_rsp_count), rsp_count_(0) {}
};

class Proposer {
    public:
        explicit Proposer(std::shared_ptr<Configure> config);
        ~Proposer() {}

        void SetPlogMng(std::shared_ptr<PlogMng> mng) { pmn_ = std::move(mng); }
        void SetConnMng(std::shared_ptr<ConnMng> mng) { conn_ = std::move(mng); }

        // propose a value
        int Propose(uint64_t opaque, const std::string& val, uint64_t paxos_inst = 0);

    private:
        int doAccept(std::shared_ptr<PaxosMsg>& p);
        int doPrepare(std::shared_ptr<PaxosMsg>& p);
        bool canSkipPrepare(uint64_t pinst, uint64_t entry);
        std::shared_ptr<BatchRpcContext> doBatchRpcRequest(int majority, std::shared_ptr<PaxosMsg>& pm);

    private:
        std::shared_ptr<PlogMng> pmn_;
        std::shared_ptr<ConnMng> conn_;
        std::shared_ptr<Configure> config_;
};

}

#endif
