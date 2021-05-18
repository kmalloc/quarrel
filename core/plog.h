#ifndef __QUARREL_ENTRY_H_
#define __QUARREL_ENTRY_H_

#include "plog.h"
#include "ptype.h"
#include "config.h"
#include "logger.h"

#include "stl.hpp"
#include "idgen.hpp"
#include "lrumap.hpp"

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
  Entry(const Configure& config, uint64_t pinst, uint64_t entry)
      : ig_(config.local_id_ + config.pid_cookie_, config.total_acceptor_),
        vig_(0xff, 1),
        pinst_(pinst),
        entry_(entry) {}

  uint64_t GenValueId() { return vig_.GetAndInc(); }
  uint64_t GenPrepareId() { return ig_.GetAndInc(); }

  void SetAccepted(std::shared_ptr<Proposal> p) { accepted_ = std::move(p); }
  void SetPromised(std::shared_ptr<Proposal> p) { promised_ = std::move(p); }
  uint64_t SetPrepareIdGreaterThan(uint64_t val) { return ig_.SetGreatThan(val); }

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
  IdGen ig_;         // proposal id generator
  IdGenByDate vig_;  // value id generator
  uint64_t pinst_;
  uint64_t entry_;
  std::shared_ptr<Proposal> accepted_;  // proposal accepted
  std::shared_ptr<Proposal> promised_;  // prepare request promised
};

// An EntryMng represents one instance of plog(an array of log entry)
// and it is supposed to be mutated from one thread only.
class EntryMng {
 public:
  EntryMng(std::shared_ptr<Configure> config, uint64_t pinst,
           uint32_t entryCacheSize = 100000)
      : db_(config->local_storage_path_),
        config_(std::move(config)),
        pinst_(pinst),
        entries_(entryCacheSize) {}

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

  // create entry if it does not exist
  Entry& GetEntryAndCreateIfNotExist(uint64_t entry);

  Entry& CreateEntry(uint64_t entry);
  uint64_t GenPrepareId(uint64_t entry);

  uint64_t GenValueId(uint64_t entry, uint64_t pid);
  void SetGlobalMaxChosenEntry(uint64_t entry);

  // return new value
  uint64_t SetPrepareIdGreaterThan(uint64_t entry, uint64_t val);

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
  uint64_t max_committed_entry_{0};
  uint64_t max_continue_chosen_entry_{0};
  uint64_t first_valid_entry_{~0ull};  // ~0 indicates empty.
  uint64_t max_in_used_entry_{~0ull};
  uint64_t last_chosen_entry_{~0ull};
  uint64_t first_unchosen_entry_{~0ull};
  uint64_t global_max_chosen_entry_{~0ull};

  LruMap<uint64_t, std::unique_ptr<Entry>> entries_;
};

using EntryMngCreator = std::function<std::unique_ptr<EntryMng>(
    int pinst, std::shared_ptr<Configure> config)>;

class PlogMng {
 public:
  PlogMng(std::shared_ptr<Configure> config) : config_(std::move(config)) {}

  int InitPlog();

  Entry& GetEntryAndCreateIfNotExist(uint64_t pinst, uint64_t entry) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->GetEntryAndCreateIfNotExist(entry);
  }

  int SetPromised(const Proposal& p) {
      auto pinst = p.plid_;
      pinst = pinst % entries_.size();
      return entries_[pinst]->SetPromised(p);
  }

  int ClearPromised(uint64_t pinst, uint64_t entry) {
      pinst = pinst % entries_.size();
      return entries_[pinst]->ClearPromised(pinst, entry);
  }

  int SetAccepted(const Proposal& p) {
      auto pinst = p.plid_;
      pinst = pinst % entries_.size();
      return entries_[pinst]->SetAccepted(p);
  }

  int SetChosen(uint64_t pinst, uint64_t entry) {
      pinst = pinst % entries_.size();
      return entries_[pinst]->SetChosen(entry);
  }

  uint64_t GetNextEntry(uint64_t pinst, bool create) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->GetNextEntry(create);
  }

  uint64_t GetMaxChosenEntry(uint64_t pinst) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->GetLastChosenEntry();
  }

  uint64_t GenValueId(uint64_t pinst, uint64_t entry, uint64_t pid) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->GenValueId(entry, pid);
  }

  uint64_t GenPrepareId(uint64_t pinst, uint64_t entry) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->GenPrepareId(entry);
  }

  uint64_t SetPrepareIdGreaterThan(uint64_t pinst, uint64_t entry, uint64_t v) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->SetPrepareIdGreaterThan(entry, v);
  }

  void SetGlobalMaxChosenEntry(uint64_t pinst, uint64_t entry) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->SetGlobalMaxChosenEntry(entry);
  }

  void SetEntryMngCreator(EntryMngCreator creator) {
    creator_ = std::move(creator);
  }

  bool IsLocalChosenLagBehind(uint64_t pinst);

 private:
  EntryMngCreator creator_;
  std::shared_ptr<Configure> config_;
  std::vector<std::unique_ptr<EntryMng>> entries_;
};
}  // namespace quarrel

#endif
