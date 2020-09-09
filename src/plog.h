#ifndef __QUARREL_ENTRY_H_
#define __QUARREL_ENTRY_H_

#include "plog.h"
#include "ptype.h"
#include "config.h"
#include "idgen.hpp"
#include "lrumap.hpp"

#include <vector>
#include <string.h>

namespace quarrel {
struct EntryRaw {
  uint32_t size_;
  uint32_t version_;
  uint64_t last_value_id_;
  uint64_t last_prepare_id_;
  char data[1];  // proposal + promised
} __attribute__((packed, aligned(1)));

class Entry {
 public:
  Entry(const Configure& config, uint64_t pinst, uint64_t entry)
      : ig_(config.local_id_, config.total_acceptor_),
        vig_(0xff, 1),
        pinst_(pinst),
        entry_(entry) {}

  uint64_t GenValueId() { return vig_.GetAndInc(); }
  uint64_t GenPrepareId() { return ig_.GetAndInc(); }
  void SetProposal(std::shared_ptr<Proposal> p) { pp_ = std::move(p); }
  void SetPromise(std::shared_ptr<Proposal> p) { promised_ = std::move(p); }
  uint64_t SetPrepareIdGreaterThan(uint64_t val) {
    return ig_.SetGreatThan(val);
  }

  const std::shared_ptr<Proposal>& GetProposal() const { return pp_; }
  const std::shared_ptr<Proposal>& GetPromised() const { return promised_; }

  int Choose() {
      if (!pp_) {
          return kErrCode_PROPOSAL_NOT_EXIST;
      }

      pp_->status_ = kPaxosState_CHOSEN;
      return kErrCode_OK;
  }

  uint32_t SerializeTo(std::string& output) {
    Proposal dummy;
    memset(&dummy, 0xff, sizeof(dummy));

    auto proposal_sz = ProposalHeaderSz + (pp_ ? pp_->size_ : 1);
    auto promised_sz = ProposalHeaderSz + (promised_ ? promised_->size_ : 1);
    auto total_sz =
        static_cast<uint32_t>(sizeof(EntryRaw) - 1 + proposal_sz + promised_sz);

    output.clear();
    output.resize(total_sz);

    EntryRaw* raw = reinterpret_cast<EntryRaw*>(&output[0]);
    raw->version_ = 0x11;
    raw->size_ = total_sz;
    raw->last_value_id_ = vig_.Get();
    raw->last_prepare_id_ = ig_.Get();

    Proposal* p1 = pp_.get();
    if (p1 == NULL) p1 = &dummy;

    Proposal* p2 = promised_.get();
    if (p2 == NULL) p2 = &dummy;

    memcpy(raw->data, p1, proposal_sz);
    memcpy(raw->data + proposal_sz, p2, promised_sz);
    return total_sz;
  }

  int UnserializeFrom(const std::string& from) {
    if (from.size() < sizeof(EntryRaw)) return kErrCode_UNMARSHAL_PLOG_FAIL;

    const EntryRaw* raw = reinterpret_cast<const EntryRaw*>(from.data());
    if (raw->size_ != from.size()) return kErrCode_INVALID_PLOG_DATA;

    vig_.SetGreatThan(raw->last_value_id_);
    ig_.SetGreatThan(raw->last_prepare_id_);

    const Proposal* p1 = reinterpret_cast<const Proposal*>(raw->data);
    const Proposal* p2 = reinterpret_cast<const Proposal*>(raw->data + ProposalHeaderSz + p1->size_);

    if (p1->size_ + p2->size_ + 2 * ProposalHeaderSz !=
        from.size() - sizeof(EntryRaw) + 1) {
      return kErrCode_INVALID_PLOG_DATA;
    }

    pp_ = std::move(CloneProposal(*p1));
    promised_ = std::move(CloneProposal(*p2));
    return kErrCode_OK;
  }

 private:
  IdGen ig_;
  IdGenByDate vig_;  // value IdGen
  uint64_t pinst_;
  uint64_t entry_;
  std::shared_ptr<Proposal> pp_;        // proposal accepted
  std::shared_ptr<Proposal> promised_;  // prepare request promised
};

// An EntryMng maintains one instance of plog(an array of log entry)
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

  virtual int Checkpoint(uint64_t pinst, uint64_t term) = 0;
  virtual int LoadEntry(uint64_t pinst, uint64_t entry, Entry&) = 0;
  virtual int SaveEntry(uint64_t pinst, uint64_t entry, const Entry&) = 0;
  virtual int LoadUncommittedEntry(std::vector<std::unique_ptr<Entry>>& entries) = 0;

  uint64_t GetMaxChosenEntry(uint64_t pinst) {
    (void)pinst;
    return last_chosen_entry_;
  }

  int SetPromised(const Proposal& p) {
    auto entry = p.pentry_;
    auto pc = CloneProposal(p);
    Entry& ent = GetEntry(entry);
    ent.SetPromise(std::move(pc));
    return SaveEntry(pinst_, entry, ent);
  }

  int ClearPromised(uint64_t pinst, uint64_t entry) {
    (void)pinst;
    Entry& ent = GetEntry(entry);
    ent.SetPromise(NULL);
    return SaveEntry(pinst_, entry, ent);
  }

  int SetAccepted(const Proposal& p) {
    auto entry = p.pentry_;
    auto pc = CloneProposal(p);
    Entry& ent = GetEntry(entry);
    ent.SetProposal(std::move(pc));
    return SaveEntry(pinst_, entry, ent);
  }

  // create entry if it does not exist
  Entry& GetEntry(uint64_t entry) {
    auto ptr = entries_.GetPtr(entry);
    if (ptr == NULL) {
      return CreateEntry(entry);
    }

    return **ptr;
  }

  Entry& CreateEntry(uint64_t entry) {
    auto ptr = entries_.GetPtr(entry);
    if (ptr) return **ptr;

    auto ent = std::unique_ptr<Entry>(new Entry(*config_, pinst_, entry));

    Entry& val = *ent;
    entries_.Put(entry, std::move(ent));

    if (LoadEntry(pinst_, entry, val) != kErrCode_OK) {
    }

    return val;
  }

  int ChooseEntry(uint64_t entry) {
      auto& ent = GetEntry(entry);
      auto ret = ent.Choose();
      if (ret != kErrCode_OK) return ret;

      ret = SaveEntry(pinst_, entry, ent);
      if (ret == kErrCode_OK && last_chosen_entry_ < entry) {
        last_chosen_entry_ = entry;
      }
      return ret;
  }

  uint64_t GenPrepareId(uint64_t entry) {
    Entry& ent = GetEntry(entry);
    return ent.GenPrepareId();
  }

  uint64_t GenValueId(uint64_t entry, uint64_t pid) {
    (void)pid;  // eliminate warning
    Entry& ent = GetEntry(entry);
    return ent.GenValueId();
  }

  // return new value
  uint64_t SetPrepareIdGreaterThan(uint64_t entry, uint64_t val) {
    Entry& ent = GetEntry(entry);
    return ent.SetPrepareIdGreaterThan(val);
  }

 private:
  std::string db_;
  std::shared_ptr<Configure> config_;

  uint64_t pinst_;
  uint64_t last_chosen_entry_{0};
  uint64_t first_unchosen_entry_{0};
  uint64_t global_max_chosen_entry_{0};

  LruMap<uint64_t, std::unique_ptr<Entry>> entries_;
};

using EntryMngCreator = std::function<std::unique_ptr<EntryMng>(
    int pinst, std::shared_ptr<Configure> config)>;

class PlogMng {
 public:
  PlogMng(std::shared_ptr<Configure> config) : config_(std::move(config)) {}

  int InitPlog() {
    entries_.clear();
    entries_.reserve(config_->plog_inst_num_);

    for (auto i = 0u; i < config_->plog_inst_num_; ++i) {
      entries_.push_back(creator_(i, config_));
    }

    return 0;
  }

  Entry& GetEntry(uint64_t pinst, uint64_t entry) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->GetEntry(entry);
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

  int ChooseEntry(uint64_t pinst, uint64_t entry) {
      pinst = pinst % entries_.size();
      return entries_[pinst]->ChooseEntry(entry);
  }

  uint64_t GetMaxChosenEntry(uint64_t pinst) {
    pinst = pinst % entries_.size();
    return entries_[pinst]->GetMaxChosenEntry(pinst);
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

  void SetEntryMngCreator(EntryMngCreator creator) {
    creator_ = std::move(creator);
  }

 private:
  EntryMngCreator creator_;
  std::shared_ptr<Configure> config_;
  std::vector<std::unique_ptr<EntryMng>> entries_;
};
}  // namespace quarrel

#endif
