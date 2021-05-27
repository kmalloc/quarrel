#include "gtest/gtest.h"

#include "plog.h"

using namespace quarrel;

class DummyEntryMngForLoad : public EntryMng {
 public:
  PlogMetaInfo meta_;

 public:
  DummyEntryMngForLoad(std::shared_ptr<Configure> conf, uint64_t pinst)
      : EntryMng(std::move(conf), pinst, 0) {
    meta_.last_committed_ = 300;
  }

  virtual int LoadEntry(uint64_t, uint64_t, Entry&) {
    return 0;
  }

  virtual int SaveEntry(uint64_t, uint64_t, const Entry&) {
    return 0;
  }

  virtual int LoadPlogMetaInfo(uint64_t, PlogMetaInfo& meta) {
    meta = meta_;
    return 0;
  }

  // NOTE: Commit() should make sure that both the entry and the max_committed_entry_ will be saved atomically.
  // otherwise thosen chosen entry smaller than max_committed_entry will be committed multiple times.
  virtual int CommitChosen(uint64_t, const Entry&, uint64_t max_committed_entry) {
    meta_.last_committed_ = max_committed_entry;
    return 0;
  }

  // end_entry == ~0ull indicates to load all entry
  virtual int BatchLoadEntry(uint64_t, uint64_t begin_entry,
                             uint64_t end_entry, std::vector<std::unique_ptr<Entry>>& entries) {
    if (end_entry == ~0ull) {
      end_entry = 1000;
    }

    int count = 0;
    for (auto i = 0; count < 110 && i + begin_entry < end_entry; i++) {
      auto eid = i + begin_entry;
      auto ent = make_unique<Entry>(pinst_, eid);
      if (eid < 180) {
        continue;
      } else if (eid < 500) {
        auto p = AllocProposal(111);
        ent->SetAccepted(p);
        ent->SetChosen();
      } else if (eid < 800) {
        auto p = AllocProposal(111);
        p->status_ = kPaxosState_ACCEPTED;
        ent->SetAccepted(p);
      } else {
        auto p = AllocProposal(111);
        p->status_ = kPaxosState_PROMISED;
        ent->SetPromised(p);
      }

      ++count;
      entries.push_back(std::move(ent));
    }

    if (begin_entry >= 1000) {
      return kErrCode_ENTRY_NOT_EXIST;
    }

    return 0;
  }
};

TEST(quarrel_plog, test_load_all_from_disk) {
  auto config = std::make_shared<Configure>();
  DummyEntryMngForLoad mng(config, 233);

  ASSERT_TRUE(mng.LoadAllFromDisk());
  ASSERT_EQ(999, mng.GetMaxInUsedEnry());
  ASSERT_EQ(499, mng.GetLastChosenEntry());
  ASSERT_EQ(500, mng.GetFirstUnchosenEntry());
  ASSERT_EQ(499, mng.GetGlobalMaxChosenEntry());
  ASSERT_EQ(300, mng.GetMaxCommittedEntry());
  ASSERT_EQ(499, mng.GetMaxContinueChosenEntry());
  ASSERT_EQ(180, mng.GetFirstValidEntry());
}

TEST(quarrel_plog, test_entry_serialization) {
  Configure config;
  config.total_acceptor_ = 23;

  Entry ent(3, 122);

  auto p1 = AllocProposal(233);
  auto p2 = AllocProposal(133);

  p1->pid_ = 444;
  p1->term_ = 555;
  p1->plid_ = 3;
  p1->pentry_ = 122;
  p1->proposer_ = 1;
  p1->opaque_ = 0xbadf00d;
  p1->value_id_ = 0xbadf11d;
  p1->status_ = kPaxosState_PROMISED;
  strcpy(reinterpret_cast<char*>(p1->data_), "dummy data for p1");

  p2->pid_ = 333;
  p2->term_ = 666;
  p2->plid_ = 3;
  p2->pentry_ = 122;
  p2->proposer_ = 2;
  p2->opaque_ = 0xbadf22d;
  p2->value_id_ = 0xbadf33d;
  p2->status_ = kPaxosState_ACCEPTED;
  strcpy(reinterpret_cast<char*>(p2->data_), "dummy data for p2");

  ent.SetPromised(p1);
  ent.SetAccepted(p2);

  std::string to;

  Entry ent2(44, 111);
  ASSERT_EQ(ent.SerializeTo(to), p2->size_ + EntryHeadSize() + 2 * ProposalHeaderSz);
  ASSERT_EQ(kErrCode_OK, ent2.UnserializeFrom(to));

  const auto& pp1 = ent2.GetProposal();
  const auto& pp2 = ent2.GetPromised();

  auto dummy = GenDummyProposal();
  ASSERT_EQ(0, memcmp(p2.get(), pp1.get(), ProposalHeaderSz + pp1->size_));
  ASSERT_EQ(0, memcmp(&dummy, pp2.get(), ProposalHeaderSz + pp2->size_));
}
