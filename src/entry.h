#ifndef __QUARREL_ENTRY_H_
#define __QUARREL_ENTRY_H_

#include "ptype.h"

namespace quarrel {
    class EntryMng {
        public:
            virtual int GetMaxCommitedId(uint64_t plid) = 0;
            virtual int Checkpoint(uint64_t plid, uint64_t term) = 0;
            virtual int OnChosen(const Proposal& p) = 0;
            virtual int SavePlog(const Proposal& p) = 0;
            virtual int LoadPlog(uint64_t plid, int entry, Proposal& p) = 0;
    };
}

#endif
