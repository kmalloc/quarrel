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
        int Propose(ConnMng& conn, uint64_t opaque, const std::string& val);

    private:
        int Accept(ConnMng& conn, const Proposal& p);
        int Prepare(ConnMng& conn, const Proposal& p);

    private:
        IdGen idgen_;
};

}

#endif
