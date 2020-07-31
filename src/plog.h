#ifndef __QUARREL_ENTRY_H_
#define __QUARREL_ENTRY_H_

#include "plog.h"
#include "ptype.h"
#include "config.h"
#include "idgen.hpp"

#include <vector>

namespace quarrel {
    class Entry {
        public:

        private:
            IdGen ig_;
            PaxosStateMachine state_;
    };

    class EntryMng {
        public:
            virtual ~EntryMng();

            virtual int GetMaxCommitedId(uint64_t plid) = 0;
            virtual int Checkpoint(uint64_t plid, uint64_t term) = 0;
            virtual int OnChosen(const Proposal& p) = 0;
            virtual int SavePlog(const Proposal& p) = 0;
            virtual int LoadPlog(uint64_t plid, int entry, Proposal& p) = 0;

        private:
            uint64_t local_chosen_entry_;
            uint64_t global_chosen_entry_;
    };


    using EntryMngCreator = std::unique_ptr<EntryMng>(int, const std::string&);

    class PlogMng {
        public:
            PlogMng(std::shared_ptr<Configure> config);

        private:
            EntryMngCreator creator_;
            std::vector<EntryMng> entries_;
    };
}

#endif
