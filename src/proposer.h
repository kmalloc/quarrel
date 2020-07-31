#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "conn.h"
#include "ptype.h"
#include "idgen.hpp"

#include <vector>

namespace quarrel {

class Proposer {
    public:
        // propose a value asychonously
        int Propose(const Proposal& p);

    private:
        int Accept(const Proposal& p);
        int Prepare(const Proposal& p);

    private:
        IdGen* idgen_;
        ConnMng* conn_mng_;
};

}

#endif
