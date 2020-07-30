#ifndef __QUARREL_PAXOS_H_
#define __QUARREL_PAXOS_H_

#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

#include "ptype.h"
#include "proposer.h"
#include "acceptor.h"
#include "config.h"

namespace quarrel {

    class Paxos {
        public:
            // config_file: json config file path
            explicit Paxos(const std::string& config_file);
            ~Paxos();

            int StartWorker();

            // submit local chosen-proposal to db
            int SubmitPendingProposal();

            // try to propose a new value.
            // empty value indicates a read probe, testing whether local is up to date.
            int Propose(uint64_t opaque, std::string value);

            virtual int GetMaxCommitedId() = 0;
            virtual int OnSubmit(const Proposal& p) = 0;
            virtual int SavePlog(const Proposal& p) = 0;
            virtual int LoadPlog(int pentry, Proposal& p) = 0;

        private:
            Paxos(const Paxos&) = delete;
            Paxos& operator=(const Paxos&) = delete;

            IdGen idgen_;
            Proposer proposer_;
            Acceptor acceptor_;
    };

}

#endif
