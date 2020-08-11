#include "acceptor.h"

#include <assert.h>

namespace quarrel {
    int Acceptor::AddMsg(std::shared_ptr<PaxosMsg> msg) {
        if (!started_) return kErrCode_WORKER_NOT_STARTED;

        auto pp = GetProposalFromMsg(msg.get());
        auto pinst = pp->plid_;
        auto idx = pinst%workers_.size();

        workers_[idx]->msg_.Enqueue(std::move(msg), false);
        if (workers_[idx]->pending_.fetch_add(1) == 0) {
            workers_[idx]->wg_.Notify();
        }
        return kErrCode_OK;
    }

    int Acceptor::StartWorker() {
        if (started_) return kErrCode_WORKER_ALREADY_STARTED;

        auto num = config_->acceptor_worker_count_;
        assert(config_->acceptor_worker_count_ < config_->plog_inst_num_);

        run_ = 1;
        workers_.clear();
        workers_.reserve(num);

        for (auto i = 0u; i < num; i++) {
            auto wd = std::unique_ptr<WorkerData>(new WorkerData);
            wd->wg_.Reset(1);
            wd->msg_.Init(config_->worker_msg_queue_sz_);
            wd->th_ = std::thread(&Acceptor::HandleMsg, this);
            workers_.push_back(std::move(wd));
        }

        started_ = true;
        return kErrCode_OK;
    }

    int Acceptor::StopWorker() {
        run_ = 0;
        for (auto i = 0u; i < workers_.size(); i++) {
            workers_[i]->th_.join();
        }
        started_ = false;
        return kErrCode_OK;
    }
}
