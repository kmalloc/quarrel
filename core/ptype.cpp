#include "ptype.h"

#include <string.h>

namespace quarrel {
    std::shared_ptr<Proposal> AllocProposal(uint32_t value_size) {
        auto total_sz = ProposalHeaderSz + value_size;
        auto rp = reinterpret_cast<Proposal*>(malloc(total_sz));
        if (rp == NULL) return NULL;

        rp->size_  = value_size;
        std::shared_ptr<Proposal> p(rp, free);
        return p;
    }

    std::shared_ptr<PaxosMsg> AllocProposalMsg(uint32_t value_size) {
        // value_size == 0 is permitted
        auto total_sz = PaxosMsgHeaderSz + ProposalHeaderSz + value_size;
        auto rp = reinterpret_cast<PaxosMsg*>(malloc(total_sz));
        if (rp == NULL) return NULL;

        auto pp = reinterpret_cast<Proposal*>(rp->data_);

        pp->size_  = value_size;

        rp->magic_ = 0xbadf00d;
        rp->size_  = static_cast<uint32_t>(ProposalHeaderSz + value_size);

        std::shared_ptr<PaxosMsg> p(rp, free);
        return p;
    }

    std::shared_ptr<Proposal> CloneProposal(const Proposal& pm) {
        auto vsz = pm.size_;
        auto pp = AllocProposal(vsz);
        memcpy(pp.get(), &pm, ProposalHeaderSz + vsz);
        return pp;
    }

    std::shared_ptr<PaxosMsg> CloneProposalMsg(const PaxosMsg& pm) {
        auto pp = reinterpret_cast<const Proposal*>(pm.data_);
        auto value_size = pp->size_;

        auto rp = AllocProposalMsg(value_size);
        memcpy(rp.get(), &pm, PaxosMsgHeaderSz + ProposalHeaderSz + value_size);
        return rp;
    }
}
