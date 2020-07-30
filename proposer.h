#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "ptype.h"
#include "idgen.hpp"

namespace quarrel {

class Proposer {
    public:
        int Propose(const Proposal& p);

    private:
        int Accept(const Proposal& p);
        int Prepare(const Proposal& p);

    private:
        IdGen* idgen_;
};

}

#endif
