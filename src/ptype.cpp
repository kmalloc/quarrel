#include "ptype.h"


namespace quarrel {
    PaxosMsgPtr AllocProposalMsg(uint32_t value_size) {
        // value_size == 0 is permitted
        auto total_sz = PaxosMsgHeaderSz + ProposalHeaderSz + value_size;
        auto rp = reinterpret_cast<PaxosMsg*>(malloc(total_sz));
        if (rp == NULL) return NULL;

        auto pp = reinterpret_cast<Proposal*>(rp->data_);

        pp->size_ = value_size;
        rp->size_ = ProposalHeaderSz + value_size;

        PaxosMsgPtr p(rp);
        return p;
    }
}
