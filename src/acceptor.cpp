#include "acceptor.h"

namespace quarrel {

    int Acceptor::AddMsg(std::shared_ptr<PaxosMsg> msg) {
        auto pp = GetProposalFromMsg(msg.get());
        auto pinst = pp->plid_;
        auto idx = pinst%msg_.size();

        msg_[idx].Enqueue(std::move(msg), false);
        return kErrCode_OK;
    }
}
