#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "conn.h"
#include "config.h"
#include "ptype.h"
#include "idgen.hpp"

#include <vector>
#include <memory>

namespace quarrel {

class Proposer {
    public:
        explicit Proposer(std::shared_ptr<Configure> config);

        // propose a value asychonously
        int Propose(ConnMng& conn, uint64_t opaque, const std::string& val, uint64_t paxos_inst = 0);

    private:
        int Accept(ConnMng& conn, const Proposal& p);
        int Prepare(ConnMng& conn, const Proposal& p);

    private:
        std::shared_ptr<Configure> config_;
};

}

#endif
