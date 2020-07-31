#include "proposer.h"
#include "logger.h"

namespace quarrel {

    Proposer::Proposer(std::shared_ptr<Configure> config)
        :config_(std::move(config)) {}

    int Proposer::Propose(uint64_t opaque, const std::string& val, uint64_t pinst) {
        auto pm = AllocProposalMsg(val.size());
        if (!pm) return kPaxosErrCode_OOM;

        auto local_conn = conn_->GetLocalConn();
        auto entry = pmn_->GetMaxCommitedEntry(pinst);

        int ret = 0;
        if (!canSkipPrepare(pinst, entry)) {
            ret = doPrepare(pm);
        }

        if (ret) {
            LOG_ERR << "propose failed, pinst:" << pinst << ", entry:" << entry << ", opaque:" << opaque;
            return ret;
        }

        return doAccept(pm);
    }

    int Propose::doPrepare(PaxosMsgPtr& p) {
        return 0;
    }
}
