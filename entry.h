#ifndef __QUARREL_ENTRY_H_
#define __QUARREL_ENTRY_H_

#include "ptype.h"

namespace quarrel {
    class EntryMng {
        public:
            virtual int GetMaxCommitedId() = 0;
            virtual int OnChosen(const Proposal& p) = 0;
            virtual int SavePlog(const Proposal& p) = 0;
            virtual int LoadPlog(int pentry, Proposal& p) = 0;
    };
}

#endif
