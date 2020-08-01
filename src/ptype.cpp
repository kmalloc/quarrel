#include "ptype.h"


namespace quarrel {
    std::shared_ptr<PaxosMsg> AllocProposalMsg(uint32_t value_size) {
        // value_size == 0 is permitted
        auto total_sz = PaxosMsgHeaderSz + ProposalHeaderSz + value_size;
        auto rp = reinterpret_cast<PaxosMsg*>(malloc(total_sz));
        if (rp == NULL) return NULL;

        auto pp = reinterpret_cast<Proposal*>(rp->data_);

        pp->size_  = value_size;

        rp->magic_ = 0xbadf00d;
        rp->size_  = ProposalHeaderSz + value_size;

        std::shared_ptr<PaxosMsg> p(rp, free);
        return p;
    }
}
