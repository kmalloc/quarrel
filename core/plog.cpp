#include "plog.h"
#include <string.h>

namespace quarrel {
// plog entry stored on disk
struct EntryRaw {
  uint32_t size_;
  uint32_t version_;
  uint64_t last_value_id_;
  uint64_t last_prepare_id_;
  char data[1];  // may contain two parts: proposed or promised proposal
} __attribute__((packed, aligned(1)));

static Proposal s_dummy_ = GenDummyProposal();

uint64_t EntryHeadSize() {
  return sizeof(EntryRaw);
}

uint32_t Entry::SerializeTo(std::string& output) {
  auto proposal_sz = ProposalHeaderSz + (accepted_ ? accepted_->size_ : 1);
  auto promised_sz = ProposalHeaderSz + ((promised_ && !accepted_) ? promised_->size_ : 1);
  auto total_sz = static_cast<uint32_t>(sizeof(EntryRaw) - 1 + proposal_sz + promised_sz);

  output.clear();
  output.resize(total_sz);

  EntryRaw* raw = reinterpret_cast<EntryRaw*>(&output[0]);
  raw->version_ = 0x11;
  raw->size_ = total_sz;
  raw->last_value_id_ = vig_.Get();
  raw->last_prepare_id_ = ig_.Get();

  Proposal* p1 = accepted_.get();
  if (p1 == NULL) p1 = &s_dummy_;

  Proposal* p2 = promised_.get();
  if (p2 == NULL || p1 != &s_dummy_) p2 = &s_dummy_;  // if accepted then promised value is no longer needed.

  memcpy(raw->data, p1, proposal_sz);
  memcpy(raw->data + proposal_sz, p2, promised_sz);
  return total_sz;
}

int Entry::UnserializeFrom(const std::string& from) {
  if (from.size() < sizeof(EntryRaw)) return kErrCode_UNMARSHAL_PLOG_FAIL;

  const EntryRaw* raw = reinterpret_cast<const EntryRaw*>(from.data());
  if (raw->size_ != from.size()) {
    LOG_ERR << "from size:" << from.size() << ", expected sz:" << raw->size_;
    return kErrCode_INVALID_PLOG_DATA;
  }

  vig_.SetGreatThan(raw->last_value_id_);
  ig_.SetGreatThan(raw->last_prepare_id_);

  const Proposal* p1 = reinterpret_cast<const Proposal*>(raw->data);
  const Proposal* p2 = reinterpret_cast<const Proposal*>(raw->data + ProposalHeaderSz + p1->size_);

  if (p1->size_ + p2->size_ + 2 * ProposalHeaderSz != from.size() - sizeof(EntryRaw) + 1) {
    return kErrCode_INVALID_PLOG_DATA;
  }

  accepted_ = std::move(CloneProposal(*p1));
  promised_ = std::move(CloneProposal(*p2));
  return kErrCode_OK;
}

// EntryMng definition.
bool EntryMng::RecoverRange(uint64_t start_entry, uint64_t end_entry) {
  std::vector<std::unique_ptr<Entry>> entries;
  entries.reserve(entries_.Capacity() / 2);

  while (1) {
    entries.clear();
    auto ret = BatchLoadEntry(pinst_, start_entry, end_entry, entries);
    if (ret != kErrCode_OK) {
      LOG_ERR << "entry batch load failed for RecoverFromDisk, ret:" << ret;
      break;
    } else if (entries.empty()) {
      break;
    }

    for (auto j = 0u; j < entries.size(); j++) {
      auto entry_id = entries[j]->EntryID();
      if (entries[j]->Status() == kPaxosState_CHOSEN) {
        if (entry_id > last_chosen_entry_) {
          last_chosen_entry_ = entry_id;
        }
        if (entry_id == max_continue_chosen_entry_ + 1) {
          max_continue_chosen_entry_++;
        }
      } else if (entry_id < first_unchosen_entry_) {
        first_unchosen_entry_ = entry_id;
      }

      if (entry_id < first_valid_entry_) {
        first_valid_entry_ = entry_id;
      }

      if (entry_id > max_in_used_entry_) {
        max_in_used_entry_ = entry_id;
      }

      if (entry_id >= start_entry) {
        start_entry = entry_id + 1;
      }

      entries_.Put(entry_id, std::move(entries[j]));
    }
  }

  if (last_chosen_entry_ > global_max_chosen_entry_) {
    global_max_chosen_entry_ = last_chosen_entry_;
  }

  return kErrCode_OK;
}

bool EntryMng::LoadAllFromDisk() {
  PlogMetaInfo meta;
  auto ret = LoadPlogMetaInfo(pinst_, meta);
  if (ret != kErrCode_OK) {
    LOG_ERR << "load metainfo failed for RecoverFromDisk, ret:" << ret;
    return false;
  }

  max_in_used_entry_ = 0;
  last_chosen_entry_ = 0;
  first_valid_entry_ = ~0ull;
  first_unchosen_entry_ = ~0ull;
  max_committed_entry_ = meta.last_committed_;
  max_continue_chosen_entry_ = meta.last_committed_;
  global_max_chosen_entry_ = 0;

  uint64_t start_entry = 1;  // entry 0 is reserved.
  RecoverRange(start_entry, ~0ull);

  return true;
}

int EntryMng::SetChosen(uint64_t entry) {
  auto ent = GetEntry(entry);
  if (!ent) return kErrCode_ENTRY_NOT_EXIST;

  auto ret = ent->SetChosen();
  if (ret != kErrCode_OK) return ret;

  ret = SaveEntry(pinst_, entry, *ent);
  if (ret == kErrCode_OK) {
    if (last_chosen_entry_ == ~0ull || last_chosen_entry_ < entry) {
      last_chosen_entry_ = entry;
    }
    if (global_max_chosen_entry_ == ~0ull ||
        global_max_chosen_entry_ < entry) {
      global_max_chosen_entry_ = entry;
    }
  }

  return ret;
}

void EntryMng::Reset() {
  entries_.Clear();
  first_valid_entry_ = ~0ull;
  last_chosen_entry_ = ~0ull;
  max_in_used_entry_ = ~0ull;
  first_unchosen_entry_ = ~0ull;
  max_continue_chosen_entry_ = 0;
  global_max_chosen_entry_ = ~0ull;
}

uint64_t EntryMng::GetNextEntry(bool create) {
  auto ret = 0ull;
  if (last_chosen_entry_ != ~0ull) {
    ret = last_chosen_entry_ + 1;
  }

  if (create) {
    CreateEntry(ret);
  }

  return ret;
}

int EntryMng::SetPromised(const Proposal& p) {
  auto entry = p.pentry_;
  auto pc = CloneProposal(p);
  Entry* ent = GetEntry(entry);
  if (!ent) {
    return kErrCode_ENTRY_NOT_EXIST;
  }

  ent->SetPromised(std::move(pc));
  return SaveEntry(pinst_, entry, *ent);
}

int EntryMng::ClearPromised(uint64_t pinst, uint64_t entry) {
  (void)pinst;
  auto ent = GetEntry(entry);
  if (!ent) return kErrCode_PLOG_NOT_EXIST;

  ent->SetPromised(NULL);
  return SaveEntry(pinst_, entry, *ent);
}

int EntryMng::SetAccepted(const Proposal& p) {
  auto entry = p.pentry_;
  auto pc = CloneProposal(p);
  Entry* ent = GetEntry(entry);
  if (!ent) return kErrCode_ENTRY_NOT_EXIST;

  ent->SetAccepted(std::move(pc));
  return SaveEntry(pinst_, entry, *ent);
}

Entry* EntryMng::GetEntry(uint64_t entry) {
  auto ret = entries_.GetPtr(entry);
  if (ret) return ret->get();

  auto ent = make_unique<Entry>(*config_, pinst_, entry);

  if (LoadEntry(pinst_, entry, *ent) != kErrCode_OK) return NULL;

  auto ptr = ent.get();
  entries_.Put(entry, std::move(ent));
  return ptr;
}

// create entry if it does not exist
Entry& EntryMng::GetEntryAndCreateIfNotExist(uint64_t entry) {
  auto ptr = entries_.GetPtr(entry);
  if (ptr == NULL) {
    return CreateEntry(entry);
  }

  return **ptr;
}

Entry& EntryMng::CreateEntry(uint64_t entry) {
  auto ptr = entries_.GetPtr(entry);
  if (ptr) return **ptr;

  auto ent = std::unique_ptr<Entry>(new Entry(*config_, pinst_, entry));

  Entry& val = *ent;
  entries_.Put(entry, std::move(ent));

  if (max_in_used_entry_ == ~0ull || entry > max_in_used_entry_) {
    max_in_used_entry_ = entry;
    if (LoadEntry(pinst_, entry, val) != kErrCode_OK) {
      LOG_ERR << "Load entry failed, instance:" << pinst_ << ", entry id:" << entry;
    }
  }

  return val;
}

uint64_t EntryMng::GenPrepareId(uint64_t entry) {
  Entry* ent = GetEntry(entry);
  if (!ent) return ~0ull;

  return ent->GenPrepareId();
}

uint64_t EntryMng::GenValueId(uint64_t entry, uint64_t pid) {
  (void)pid;  // eliminate warning
  Entry* ent = GetEntry(entry);
  if (!ent) return ~0ull;

  return ent->GenValueId();
}

void EntryMng::SetGlobalMaxChosenEntry(uint64_t entry) {
  if (global_max_chosen_entry_ != ~0ull &&
      global_max_chosen_entry_ >= entry) {
    return;
  }

  global_max_chosen_entry_ = entry;
}

// return new value
uint64_t EntryMng::SetPrepareIdGreaterThan(uint64_t entry, uint64_t val) {
  Entry* ent = GetEntry(entry);
  if (!ent) return ~0ull;

  return ent->SetPrepareIdGreaterThan(val);
}

// PlogMng definition.
bool PlogMng::IsLocalChosenLagBehind(uint64_t pinst) {
  pinst = pinst % entries_.size();

  auto max_in_used = entries_[pinst]->GetMaxInUsedEnry();
  auto local_max_chosen = entries_[pinst]->GetLastChosenEntry();
  auto global_max_chosen = entries_[pinst]->GetGlobalMaxChosenEntry();

  if (max_in_used == ~0ull && local_max_chosen == ~0ull) return false;

  assert(max_in_used >= local_max_chosen);

  if (local_max_chosen < global_max_chosen) {
    return true;
  }

  return false;
}

int PlogMng::InitPlog() {
  entries_.clear();
  entries_.reserve(config_->plog_inst_num_);

  for (auto i = 0u; i < config_->plog_inst_num_; ++i) {
    entries_.push_back(creator_(i, config_));
  }

  return 0;
}

}  // namespace quarrel
