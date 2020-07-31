#include "proposer.h"
#include "logger.h"
#include "waitgroup.hpp"

namespace quarrel {

    Proposer::Proposer(std::shared_ptr<Configure> config)
        :config_(std::move(config)) {}

    int Proposer::Propose(uint64_t opaque, const std::string& val, uint64_t pinst) {
        auto pm = AllocProposalMsg(val.size());
        if (!pm) return kPaxosErrCode_OOM;

        auto entry = pmn_->GetMaxCommittedEntry(pinst) + 1;
        auto pid = pmn_->GenPrepareId(pinst, entry);

        pm->from_ = config_->local_id_;
        pm->version_ = config_->msg_version_;
        pm->type_ = kPaxosMsgType_PREPARE_REQ;

        auto pp = reinterpret_cast<Proposal*>(pm->data_);
        //pp->term_ = xxxxx;
        pp->pid_ = pid;
        pp->plid_ = pinst;
        pp->pentry_ = entry;
        pp->opaque_ = opaque;
        pp->proposer_ = config_->local_id_;
        memcpy(pp->data_, val.data(), val.size());

        int ret = 0;
        if (!canSkipPrepare(pinst, entry)) {
            ret = doPrepare(pm);
        }

        if (ret) {
            LOG_ERR << "do prepare failed, pinst:" << pinst << ", entry:" << entry << ", opaque:" << opaque;
            return ret;
        }

        pm->type_ = kPaxosMsgType_ACCEPT_REQ;

        return doAccept(pm);
    }

    bool Proposer::canSkipPrepare(uint64_t pinst, uint64_t entry) {
        // TODO
        return false;
    }

    int Proposer::doPrepare(PaxosMsgPtr& p) {
        // send to local conn
        // then to remote

        auto majority = config_->total_acceptor_/2 + 1;
        auto cb = [](){};
        // TODO

        WaitGroup wg(majority, config_->timeout_);
        if (!wg.Wait()) return kPaxosErrCode_TIMEOUT;

        // TODO

        return 0;
    }

    int Proposer::doAccept(PaxosMsgPtr& p) {
        // send to local conn
        // then to remote

        return 0;
    }
}
