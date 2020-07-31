#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "conn.h"
#include "plog.h"
#include "config.h"
#include "ptype.h"
#include "idgen.hpp"

#include <vector>
#include <memory>

namespace quarrel {

class Proposer {
    public:
        explicit Proposer(std::shared_ptr<Configure> config);
        ~Proposer() {}

        void SetPlogMng(std::shared_ptr<PlogMng> mng) { pmn_ = std::move(mng); }
        void SetConnMng(std::shared_ptr<ConnMng> mng) { conn_ = std::move(mng); }

        // propose a value asychonously
        int Propose(uint64_t opaque, const std::string& val, uint64_t paxos_inst = 0);

    private:
        int doAccept(PaxosMsgPtr& p);
        int doPrepare(PaxosMsgPtr& p);
        bool canSkipPrepare(uint64_t pinst, uint64_t entry);

    private:
        std::shared_ptr<PlogMng> pmn_;
        std::shared_ptr<ConnMng> conn_;
        std::shared_ptr<Configure> config_;
};

}

#endif
