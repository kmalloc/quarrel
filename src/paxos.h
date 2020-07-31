#ifndef __QUARREL_PAXOS_H_
#define __QUARREL_PAXOS_H_

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "conn.h"
#include "plog.h"
#include "ptype.h"
#include "proposer.h"
#include "acceptor.h"
#include "config.h"

namespace quarrel {

    class Paxos {
        public:
            explicit Paxos(std::unique_ptr<Configure> config);

            ~Paxos();

            int Start();
            int Stop();

            void SetConnMng(std::unique_ptr<ConnMng> mng);
            void SetEntryMng(std::unique_ptr<EntryMng> mng);

            // submit local chosen-proposal to db
            int SubmitPendingProposal();

            // try to propose a new value.
            // empty value indicates a read probe, testing whether local is up to date.
            // paxos_inst: the paxos instance to use, default to 0
            int Propose(uint64_t opaque, const std::string& value, uint64_t paxos_inst = 0);

        private:
            Paxos(const Paxos&) = delete;
            Paxos& operator=(const Paxos&) = delete;

            // these most basic info should come first.
            std::shared_ptr<Configure> config_;
            ConnMng conn_mng_;

            // those use basic info comes after.
            Acceptor acceptor_;
            Proposer proposer_;
    };

}

#endif
