#include "proposer.h"
#include "logger.h"

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
        pp->status_ = kPaxosState_PREPARED;
        pp->value_id_ = pmn_->GenValueId(pinst, pid);

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
        pp->status_ = kPaxosState_PROMISED;

        ret = doAccept(pm);

        if (ret == kErrCode_OK) {
            pm->type_ = kMsgType_CHOSEN_REQ;
            pp->status_ = kPaxosState_ACCEPTED;
            doChosen(pm);
        }

        return ret;
    }

    bool Proposer::canSkipPrepare(uint64_t pinst, uint64_t entry) {
        // TODO
        return false;
    }

    std::shared_ptr<BatchRpcContext> Proposer::doBatchRpcRequest(int majority, std::shared_ptr<PaxosMsg>& pm) {
        // shared objects must be put on heap.
        // so that when a delayed msg arrives, there won't be any memory access violation
        auto ctx = std::make_shared<BatchRpcContext>(majority);

        auto cb = [ctx](std::shared_ptr<PaxosMsg> msg)->int {
            // delayed rsp will be ignored.
            auto idx = ctx->rsp_count_.fetch_add(1);
            ctx->rsp_msg_[idx] = std::move(msg);
            ctx->wg_.Notify();
            return 0;
        };

        ctx->ret_ = kErrCode_OK;
        auto& local = conn_->GetLocalConn();
        auto& remote = conn_->GetRemoteConn();

        RpcReqData req{config_->timeout_, cb, pm};

        auto ret = 0;
        if ((ret=local->DoRpcRequest(req))) {
            ctx->ret_ = ret;
            return std::move(ctx);
        }

        for (auto i = 0; i < remote.size(); ++i) {
            remote[i]->DoRpcRequest(req);
        }

        if (!ctx->wg_.Wait(config_->timeout_)) {
            ctx->ret_ = kErrCode_TIMEOUT;
        }

        return ctx;
    }

    int Proposer::doPrepare(std::shared_ptr<PaxosMsg>& pm) {
        // send to local conn
        // then to remote

        int valid_rsp = 0;
        std::shared_ptr<PaxosMsg> last_voted;
        auto majority = config_->total_acceptor_/2 + 1;
        auto origin_proposal = reinterpret_cast<Proposal*>(pm->data_);

        auto ctx = doBatchRpcRequest(majority, pm);
        if (ctx->ret_ != kErrCode_OK) return ctx->ret_;

        for (auto idx = 0; idx < ctx->rsp_count_; ++idx) {
            std::shared_ptr<PaxosMsg> m = std::move(ctx->rsp_msg_[idx]);
            auto rsp_proposal = reinterpret_cast<Proposal*>(pm->data_);

            if (rsp_proposal->pid_ > origin_proposal->pid_) {
                // rejected
                pmn_->SetPrepaeIdGreaterThan(origin_proposal->plid_, origin_proposal->pentry_, rsp_proposal->pid_);
                continue;
            }

            if (rsp_proposal->value_id_ != origin_proposal->value_id_ || rsp_proposal->opaque_ != origin_proposal->opaque_) {
                // peer responses with last vote
                auto lastp = reinterpret_cast<Proposal*>(last_voted->data_);
                if (last_voted.get() == NULL || lastp->pid_ < rsp_proposal->pid_) {
                    // last vote with the largest prepare id.
                    last_voted = m;
                }
            }

            ++valid_rsp;
        }

        if (valid_rsp >= majority) {
            if (last_voted) {
                pm = std::move(last_voted);
            }
            return kErrCode_OK;
        }

        return kErrCode_NOT_QUORAUM;
    }

    int Proposer::doAccept(std::shared_ptr<PaxosMsg>& pm) {
        // send to local conn
        // then to remote
        auto majority = config_->total_acceptor_/2 + 1;
        auto origin_proposal = reinterpret_cast<Proposal*>(pm->data_);

        auto ctx = doBatchRpcRequest(majority, pm);
        if (ctx->ret_ != kErrCode_OK) return ctx->ret_;

        int valid_rsp = 0;
        for (auto idx = 0; idx < ctx->rsp_count_; ++idx) {
            std::shared_ptr<PaxosMsg> m = std::move(ctx->rsp_msg_[idx]);
            auto rsp_proposal = reinterpret_cast<Proposal*>(pm->data_);

            if (rsp_proposal->pid_ > origin_proposal->pid_) {
                // rejected
                pmn_->SetPrepaeIdGreaterThan(origin_proposal->plid_, origin_proposal->pentry_, rsp_proposal->pid_);
                continue;
            }

            if (rsp_proposal->value_id_ != origin_proposal->value_id_ || rsp_proposal->opaque_ != origin_proposal->opaque_) {
                // peer responses with last vote
                // not possible without fast-accept enabled.
                // FIXME
                LOG_ERR << "invalid doAccept rsp from peer(" << pm->to_ << "), value id is changed, rsp:("
                        << rsp_proposal->value_id_ << "," << rsp_proposal->opaque_ << "), origin:("
                        << origin_proposal->value_id_ << "," << rsp_proposal->opaque_ << ")";
                continue;
            }

            ++valid_rsp;
        }

        if (valid_rsp >= majority) {
            return kErrCode_OK;
        }

        return kErrCode_NOT_QUORAUM;
    }

    int Proposer::doChosen(std::shared_ptr<PaxosMsg>& pm) {
        doBatchRpcRequest(0, pm);
        return kErrCode_OK;
    }
}
