#ifndef __QUARREL_ENTRY_H_
#define __QUARREL_ENTRY_H_

#include "plog.h"
#include "ptype.h"
#include "config.h"
#include "idgen.hpp"
#include "lrumap.hpp"

#include <vector>

namespace quarrel {
    class Entry {
        public:
            Entry(uint64_t pinst, uint64_t entry);

        private:
            IdGen ig_;
            IdGenByDate value_ig_;
            PaxosStateMachine state_;
    };

    class EntryMng {
        public:
            EntryMng(uint64_t pinst, std::string db);
            virtual ~EntryMng();

            virtual int GetMaxCommitedId() = 0;
            virtual int SaveEntry(uint64_t) = 0;
            virtual int LoadEntry(uint64_t entry) = 0;
            virtual int Checkpoint(uint64_t term) = 0;

            virtual int LoadUncommittedEntry() = 0;

            uint64_t GenValueId();
            uint64_t GenPrepareId();
            int GetMaxCommittedEntry();
            int SetEntry(const Proposal& p);
            Entry* GetEntry(uint64_t entry);
            Entry* CreateEntry(uint64_t entry);
            int LoadPlog(int entry, Proposal& p);
            int SetPrepareIdGreaterThan(uint64_t entry, uint64_t val);

        private:
            std::string db_;
            uint64_t pinst_;
            uint64_t local_chosen_entry_;
            uint64_t global_chosen_entry_;
            uint64_t max_committed_entry_;

            LruMap<uint64_t, Entry> entries_;
    };

    using EntryMngCreator = std::function<std::unique_ptr<EntryMng>(int, std::string)>;

    class PlogMng {
        public:
            PlogMng(std::shared_ptr<Configure> config): config_(std::move(config)) {}

            int InitPlog() {
                // TODO
                return 0;
            }

            uint64_t LoadUncommitedEntry(uint64_t pinst) {
                pinst = pinst % entries_.size();
                return entries_[pinst]->LoadUncommittedEntry();
            }

            uint64_t GetMaxCommittedEntry(uint64_t pinst) {
                pinst = pinst % entries_.size();
                return entries_[pinst]->GetMaxCommittedEntry();
            }

            uint64_t GenValueId(uint64_t pinst, uint64_t pid) {
                pinst = pinst % entries_.size();
                return entries_[pinst]->GenValueId();
            }

            uint64_t GenPrepareId(uint64_t pinst, uint64_t entry) {
                pinst = pinst % entries_.size();
                return entries_[pinst]->GenPrepareId();
            }

            uint64_t SetPrepareIdGreaterThan(uint64_t pinst, uint64_t entry, uint64_t v) {
                pinst = pinst % entries_.size();
                return entries_[pinst]->SetPrepareIdGreaterThan(entry, v);
            }

            void SetEntryMngCreator(EntryMngCreator creator) {
                creator_ = std::move(creator);
            }

        private:
            EntryMngCreator creator_;
            std::shared_ptr<Configure> config_;
            std::vector<std::unique_ptr<EntryMng>> entries_;
    };
}

#endif
