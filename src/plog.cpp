#include "plog.h"
#include <string.h>

namespace quarrel {

int EntryMng::SetPromised(const Proposal& pr) {
    auto p = AllocProposal(pr.size_);
    if (!p) return kErrCode_OOM;

    auto entry = pr.pentry_;
    memcpy(p.get(), &pr, ProposalHeaderSz + pr.size_);

    auto ptr = entries_.GetPtr(entry);
    // if (!ptr) ptr = CreateEntry()
    // TODO

    return 0;
}

}
