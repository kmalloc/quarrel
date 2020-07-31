#ifndef __QUARREL_PAXOS_H_
#define __QUARREL_PAXOS_H_

#include <string>
#include <thread>
#include <vector>
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
            // config_file: json config file path
             Paxos(const std::string& config_file);

            ~Paxos();

            int Start();
            int Stop();

            void SetConnMng(std::unique_ptr<ConnMng> mng);
            void SetEntryMng(std::unique_ptr<EntryMng> mng);

            // submit local chosen-proposal to db
            int SubmitPendingProposal();

            // try to propose a new value.
            // empty value indicates a read probe, testing whether local is up to date.
            int Propose(uint64_t opaque, std::string value);

        private:
            Paxos(const Paxos&) = delete;
            Paxos& operator=(const Paxos&) = delete;

            Acceptor acceptor_;
            Proposer proposer_;

            std::unique_ptr<ConnMng> conn_mng_;
    };

}

#endif
