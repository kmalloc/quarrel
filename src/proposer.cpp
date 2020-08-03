#include "proposer.h"
#include "logger.h"
#include "waitgroup.hpp"

#include <atomic>

namespace quarrel {

    Proposer::Proposer(std::shared_ptr<Configure> config)
        :config_(std::move(config)) {}

    int Proposer::Propose(uint64_t opaque, const std::string& val, uint64_t pinst) {
        auto pm = AllocProposalMsg(val.size());
        if (!pm) return kErrCode_OOM;

        auto entry = pmn_->GetMaxCommittedEntry(pinst) + 1;
        auto pid = pmn_->GenPrepareId(pinst, entry);

        pm->from_ = config_->local_id_;
        pm->version_ = config_->msg_version_;
        pm->type_ = kMsgType_PREPARE_REQ;

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

        pm->type_ = kMsgType_ACCEPT_REQ;

        return doAccept(pm);
    }

    bool Proposer::canSkipPrepare(uint64_t pinst, uint64_t entry) {
        // TODO
        return false;
    }

    int Proposer::doPrepare(std::shared_ptr<PaxosMsg>& p) {
        // send to local conn
        // then to remote

        auto majority = config_->total_acceptor_/2 + 1;

        // following shared objects must be put on heap.
        // so that when a delayed msg arrives, there won't be any memory access violation
        auto sz = std::make_shared<std::atomic<int>>();
        auto wg = std::make_shared<WaitGroup>(majority);
        auto rsp = std::make_shared<std::vector<std::shared_ptr<PaxosMsg>>>();

        rsp->resize(config_->total_acceptor_);

        auto cb = [sz, wg, rsp](std::shared_ptr<PaxosMsg> msg)->int {
            auto idx = sz->fetch_add(1);
            (*rsp)[idx] = std::move(msg);
            wg->Notify();
            return 0;
        };

        int ret = 0;
        auto& local = conn_->GetLocalConn();
        auto& remote = conn_->GetRemoteConn();

        RpcReqData req = {config_->timeout_, cb, p};

        if ((ret=local->DoRpcRequest(req))) {
            return ret;
        }

        for (auto i = 0; i < remote.size(); ++i) {
            remote[i]->DoRpcRequest(req);
        }

        if (!wg->Wait(config_->timeout_)) return kErrCode_TIMEOUT;

        // TODO
        return 0;
    }

    int Proposer::doAccept(std::shared_ptr<PaxosMsg>& p) {
        // send to local conn
        // then to remote

        return 0;
    }
}
