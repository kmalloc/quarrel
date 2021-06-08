#ifndef __QUARREL_ENTRY_H_
#define __QUARREL_ENTRY_H_

#include "plog.h"
#include "ptype.h"
#include "config.h"
#include "logger.h"

#include "stl.hpp"
#include "time.hpp"
#include "lrumap.hpp"
#include "paxos_group.h"

#include <vector>
#include <assert.h>
#include <string.h>

namespace quarrel {

uint64_t EntryHeadSize();

// meta info for a plog instance(an array of paxos entry)
struct PlogMetaInfo {
  uint32_t type_;  // unused
  uint32_t version_;
  uint64_t pinst_;
  uint64_t last_committed_;  // not idempotent
} __attribute__((packed, aligned(1)));

// in-memory representation of a paxos entry.
// deserialzed from EntryRaw for existing entry.
class Entry {
 public:
  Entry(uint64_t pinst, uint64_t entry)
      : pinst_(pinst), entry_(entry) {}

  void SetAccepted(std::shared_ptr<Proposal> p) { accepted_ = std::move(p); }
  void SetPromised(std::shared_ptr<Proposal> p) { promised_ = std::move(p); }

  uint64_t EntryID() const { return entry_; }

  uint64_t Status() const {
    if (accepted_) return accepted_->status_;
    if (promised_) return promised_->status_;
    return kPaxosState_INVALID_PROPOSAL;
  }

  const std::shared_ptr<Proposal>& GetProposal() const { return accepted_; }
  const std::shared_ptr<Proposal>& GetPromised() const { return promised_; }

  int SetChosen() {
    if (!accepted_) {
      return kErrCode_PROPOSAL_NOT_EXIST;
    }

    accepted_->status_ = kPaxosState_CHOSEN;
    return kErrCode_OK;
  }

  uint32_t SerializeTo(std::string& output);
  int UnserializeFrom(const std::string& from);

 private:
  uint64_t pinst_;
  uint64_t entry_;
  std::shared_ptr<Proposal> accepted_;  // proposal accepted
  std::shared_ptr<Proposal> promised_;  // prepare request promised
};

// An EntryMng represents one instance of plog(an array of log entry)
// and it is supposed to be mutated from one thread only.
class EntryMng {
  // TODO entry recycle

 public:
  EntryMng(std::shared_ptr<Configure> config, uint64_t pinst)
      : db_(config->local_storage_path_),
        config_(std::move(config)),
        pinst_(pinst),
        entries_(config_->entry_cache_num_) {}

  virtual ~EntryMng() {}

  int SetChosen(uint64_t entry);
  bool LoadAllFromDisk();
  bool RecoverRange(uint64_t start_entry, uint64_t end_entry);

  void Reset();
  uint64_t GetNextEntry(bool create);
  int SetPromised(const Proposal& p);
  int ClearPromised(uint64_t pinst, uint64_t entry);
  int SetAccepted(const Proposal& p);
  Entry* GetEntry(uint64_t entry);

  Entry& CreateEntry(uint64_t entry);

  void SetGlobalMaxChosenEntry(uint64_t entry);

  // create entry if it does not exist
  Entry& GetEntryAndCreateIfNotExist(uint64_t entry);

  uint64_t GetMaxInUsedEnry() const { return max_in_used_entry_; }
  uint64_t GetLastChosenEntry() const { return last_chosen_entry_; }
  uint64_t GetFirstUnchosenEntry() const { return first_unchosen_entry_; }
  uint64_t GetGlobalMaxChosenEntry() const { return global_max_chosen_entry_; }
  uint64_t GetMaxCommittedEntry() const { return max_committed_entry_; }
  uint64_t GetMaxContinueChosenEntry() const { return max_continue_chosen_entry_; }
  uint64_t GetFirstValidEntry() const { return first_valid_entry_; }

 protected:
  virtual int LoadEntry(uint64_t pinst, uint64_t entry, Entry&) = 0;
  virtual int SaveEntry(uint64_t pinst, uint64_t entry, const Entry&) = 0;

  virtual int LoadPlogMetaInfo(uint64_t pinst, PlogMetaInfo& info) = 0;

  // NOTE: Commit() should make sure that both the entry and the max_committed_entry_ will be saved atomically.
  // otherwise thosen chosen entry smaller than max_committed_entry will be committed multiple times.
  virtual int CommitChosen(uint64_t pinst, const Entry& entry, uint64_t max_committed_entry) = 0;

  // end_entry == ~0ull indicates to load all entry
  virtual int BatchLoadEntry(uint64_t pinst, uint64_t begin_entry,
                             uint64_t end_entry, std::vector<std::unique_ptr<Entry>>& entries) = 0;

  // called peroidically to remove old log
  virtual int TrimPlog(uint64_t pinst, uint64_t begin_entry, uint64_t end_entry) {
    (void)pinst;
    (void)begin_entry;
    (void)end_entry;
    return 0;
  }

 protected:
  std::string db_;
  std::shared_ptr<Configure> config_;

  uint64_t pinst_;
  uint64_t last_access_time_{0};
  uint64_t max_committed_entry_{0};
  uint64_t max_continue_chosen_entry_{0};
  uint64_t first_valid_entry_{~0ull};  // ~0 indicates empty.
  uint64_t max_in_used_entry_{~0ull};
  uint64_t last_chosen_entry_{~0ull};
  uint64_t first_unchosen_entry_{~0ull};
  uint64_t global_max_chosen_entry_{~0ull};

  LruMap<uint64_t, std::unique_ptr<Entry>> entries_;
};

using EntryMngCreator = std::function<std::unique_ptr<EntryMng>(std::shared_ptr<Configure> config, uint64_t pinst)>;

class PlogMng {
 public:
  PlogMng(std::shared_ptr<Configure> config, std::shared_ptr<PaxosGroupBase> mapper)
      : config_(std::move(config)), pg_mapper_(std::move(mapper)) {}

  // FIXME: launch thread to recycle entry mng
  int InitPlog();

  Entry& GetEntryAndCreateIfNotExist(uint64_t pinst, uint64_t entry) {
    auto mng = GetEntryMngAndCreateIfNotExist(pinst);
    return mng->GetEntryAndCreateIfNotExist(entry);
  }

  int SetPromised(const Proposal& p) {
    auto pinst = p.plid_;
    auto mng = GetEntryMngAndCreateIfNotExist(pinst);
    return mng->SetPromised(p);
  }

  int ClearPromised(uint64_t pinst, uint64_t entry) {
    auto mng = GetEntryMngAndCreateIfNotExist(pinst);
    return mng->ClearPromised(pinst, entry);
  }

  int SetAccepted(const Proposal& p) {
    auto pinst = p.plid_;
    auto mng = GetEntryMngAndCreateIfNotExist(pinst);
    return mng->SetAccepted(p);
  }

  int SetChosen(uint64_t pinst, uint64_t entry) {
    auto mng = GetEntryMngAndCreateIfNotExist(pinst);
    return mng->SetChosen(entry);
  }

  uint64_t GetNextEntry(uint64_t pinst, bool create) {
    auto mng = GetEntryMngAndCreateIfNotExist(pinst);
    return mng->GetNextEntry(create);
  }

  uint64_t GetMaxChosenEntry(uint64_t pinst) {
    auto mng = GetEntryMngAndCreateIfNotExist(pinst);
    return mng->GetLastChosenEntry();
  }

  void SetGlobalMaxChosenEntry(uint64_t pinst, uint64_t entry) {
    auto mng = GetEntryMngAndCreateIfNotExist(pinst);
    return mng->SetGlobalMaxChosenEntry(entry);
  }

  void SetEntryMngCreator(EntryMngCreator creator) {
    creator_ = std::move(creator);
  }

  bool IsLocalChosenLagBehind(uint64_t pinst);

 private:
  EntryMng* GetEntryMngAndCreateIfNotExist(uint64_t pinst);

 private:
  EntryMngCreator creator_;
  std::shared_ptr<Configure> config_;
  std::shared_ptr<PaxosGroupBase> pg_mapper_;
  std::vector<std::unique_ptr<EntryMng>> mngs_;
};
}  // namespace quarrel

#endif
