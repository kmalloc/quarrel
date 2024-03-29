#include "gtest/gtest.h"

#include <string>
#include <thread>
#include <chrono>
#include <future>

#include "logger.h"

#include "acceptor.h"

using namespace quarrel;

struct DummyEntryMng : public EntryMng {
  DummyEntryMng(std::shared_ptr<Configure> config, uint64_t pinst)
      : EntryMng(std::move(config), pinst) {}

  virtual int SaveEntry(uint64_t pinst, uint64_t entry, const Entry& ent) {
    (void)pinst;
    (void)entry;
    (void)ent;

    const auto& pp = ent.GetPromised();
    if (pp) {
        if (pp->size_ && memcmp(pp->data_, "writefail", 9) == 0) {
            return kErrCode_WRITE_PLOG_FAIL;
        }
    }

    const auto& pp2 = ent.GetProposal();
    if (pp2) {
        if (pp2->size_ && memcmp(pp2->data_, "writefail", 9) == 0) {
            return kErrCode_WRITE_PLOG_FAIL;
        }
    }

    if (pp2 && pp2->status_ == kPaxosState_CHOSEN) {
        if (pp2->size_ && memcmp(pp2->data_, "miliao", 6) == 0) {
            LOG_ERR << "dummy save chosen failed";
            return kErrCode_WRITE_PLOG_FAIL;
        }
    }

    return kErrCode_OK;
  }
  virtual int LoadEntry(uint64_t pinst, uint64_t entry, Entry& ent) {
    (void)pinst;
    (void)entry;
    (void)ent;
    return kErrCode_OK;
  }

  virtual int CommitChosen(uint64_t pinst, const Entry& entry, uint64_t max_committed_entry) {
    (void)pinst;
    (void)entry;
    (void)max_committed_entry;
    return 0;
  }

  virtual int LoadPlogMetaInfo(uint64_t pinst, PlogMetaInfo& info) {
    (void)info;
    (void)pinst;
    return 0;
  }

  virtual int BatchLoadEntry(uint64_t pinst, uint64_t begin_entry,
                             uint64_t end_entry, std::vector<std::unique_ptr<Entry>>& entries) {
    (void)pinst;
    (void)begin_entry;
    (void)end_entry;
    (void)entries;
    return 0;
  }
};

TEST(acceptor_test, test_acceptor_api) {
  auto config = std::make_shared<Configure>();
  config->timeout_ = 8;  // 8ms
  config->local_ = {0, ConnType_LOCAL, "xxxx:yyy"};
  config->plog_inst_num_ = 5;
  config->total_acceptor_ = 3;
  config->acceptor_worker_count_ = 2;
  config->peer_.push_back({1, ConnType_REMOTE, "aaaa:bb"});
  config->peer_.push_back({2, ConnType_REMOTE, "aaaa2:bb2"});

  auto mapper = std::make_shared<PaxosGroup3>();
  std::shared_ptr<PlogMng> pmn = std::make_shared<PlogMng>(config, mapper);

  auto entry_mng_creator =
      [](std::shared_ptr<Configure> conf, uint64_t pinst) -> std::unique_ptr<EntryMng> {
    return make_unique<DummyEntryMng>(std::move(conf), pinst);
  };

  pmn->SetEntryMngCreator(entry_mng_creator);
  pmn->InitPlog();

  Acceptor acceptor(config);
  acceptor.SetPlogMng(pmn);

  ASSERT_EQ(kErrCode_OK,acceptor.StartWorker());

  auto m1 = AllocProposalMsg(11);
  auto m2 = AllocProposalMsg(11);
  auto p1 = GetProposalFromMsg(m1.get());
  auto p2 = GetProposalFromMsg(m2.get());
  auto tm = std::chrono::milliseconds(10);

  // test notify
  WaitGroup wg1(1), wg2(1);

  auto blockop = [&](std::shared_ptr<PaxosMsg>) -> int { wg1.Notify(); std::this_thread::sleep_for(tm); return 0; };
  auto noop = [&](std::shared_ptr<PaxosMsg>) -> int { wg2.Notify(); return 0; };

  m1->type_ = kMsgType_PREPARE_REQ;
  m2->type_ = kMsgType_PREPARE_REQ;
  p1->pid_ = 2;
  p1->pinst_ = 1;
  p1->pentry_ = 3;
  p2->pid_ = 1;
  p2->pinst_ = 1;
  p2->pentry_ = 4;

  ASSERT_EQ(kErrCode_OK, acceptor.AddMsg(m1, blockop));
  ASSERT_EQ(kErrCode_OK, acceptor.AddMsg(m2, noop));

  auto start = std::chrono::steady_clock::now();
  ASSERT_TRUE(wg1.Wait(3));
  auto end = std::chrono::steady_clock::now();
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  ASSERT_LT(diff, 3);

  start = std::chrono::steady_clock::now();
  ASSERT_FALSE(wg2.Wait(4));
  ASSERT_TRUE(wg2.Wait(8));
  end = std::chrono::steady_clock::now();

  diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  ASSERT_GT(diff, 8);

  LOG_INFO << "test return value from acceptor";

  p1->proposer_ = 2;
  p1->status_ = kPaxosState_PREPARED;
  p2->status_ = kPaxosState_PREPARED;

  std::shared_ptr<PaxosMsg> ret;

  auto verify = [&](std::shared_ptr<PaxosMsg> m) -> int {
    ret = std::move(m);
    wg1.Notify();
    return 0;
  };

  p1->pid_ = 23;
  p1->pentry_ = 5;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));

  ASSERT_TRUE(ret != NULL);
  auto rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_PROMISED, rp->status_);

  ret.reset();
  // get last promised from acceptor
  p1->pid_ = 22;
  p1->proposer_ = 3;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_PROMISED, rp->status_);
  ASSERT_EQ(23, rp->pid_);
  ASSERT_EQ(2, rp->proposer_);

  ret.reset();
  // overwrite existed promise
  p1->pid_ = 24;
  p1->proposer_ = 3;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_PROMISED, rp->status_);
  ASSERT_EQ(24, rp->pid_);
  ASSERT_EQ(3, rp->proposer_);

  // test failure to write plog to disk
  ret.reset();
  p1->pid_ = 25;
  memcpy(p1->data_, "writefail", 9);
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_PROMISED_FAILED, rp->status_);
  ASSERT_EQ(25, rp->pid_);
  ASSERT_EQ(3, rp->proposer_);

  // test accept new value
  ret.reset();
  p1->pid_ = 26;
  memcpy(p1->data_, "normal", 6);
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_PROMISED, rp->status_);
  ASSERT_EQ(26, rp->pid_);

  ret.reset();
  m1->type_ = kMsgType_ACCEPT_REQ;
  p1->status_ = kPaxosState_PROMISED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ACCEPTED, rp->status_);
  ASSERT_EQ(26, rp->pid_);

  // test write to entry which already have accepted value
  ret.reset();
  p1->pid_++;
  m1->type_ = kMsgType_ACCEPT_REQ;
  p1->status_ = kPaxosState_PROMISED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ACCEPTED, rp->status_);
  ASSERT_EQ(27, rp->pid_);

  ret.reset();
  p1->value_id_++;
  m1->type_ = kMsgType_ACCEPT_REQ;
  p1->status_ = kPaxosState_PROMISED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ACCEPTED_FAILED, rp->status_);
  ASSERT_EQ(27, rp->pid_);

  ret.reset();
  p1->value_id_--;
  m1->type_ = kMsgType_ACCEPT_REQ;
  p1->status_ = kPaxosState_PROMISED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ACCEPTED, rp->status_);
  ASSERT_EQ(27, rp->pid_);

  // test propose to entry which already accepted value
  ret.reset();
  p1->value_id_+=2;
  memcpy(p1->data_, "miliao", 6);
  m1->type_ = kMsgType_PREPARE_REQ;
  p1->status_ = kPaxosState_PREPARED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ACCEPTED, rp->status_);
  ASSERT_EQ(27, rp->pid_);
  ASSERT_EQ(0, memcmp(rp->data_, "normal", 6));

  // test accept with a smaller promise
  ret.reset();
  p1->pid_ = 30; //for prepare
  p1->pentry_ = 88;
  memcpy(p1->data_, "miliao", 6);
  m1->type_ = kMsgType_PREPARE_REQ;
  p1->status_ = kPaxosState_PREPARED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_PROMISED, rp->status_);
  ASSERT_EQ(30, rp->pid_);

  ret.reset();
  p1->pid_ = 33; //for accept
  p1->pentry_ = 88;
  memcpy(p1->data_, "miliao", 6);
  m1->type_ = kMsgType_ACCEPT_REQ;
  p1->status_ = kPaxosState_PROMISED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ACCEPTED, rp->status_);
  ASSERT_EQ(33, rp->pid_);

  // test fail write accepted plog
  ret.reset();
  p1->pid_ = 40; //for prepare
  p1->pentry_ = 89;
  memcpy(p1->data_, "miliao", 6);
  m1->type_ = kMsgType_PREPARE_REQ;
  p1->status_ = kPaxosState_PREPARED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_PROMISED, rp->status_);
  ASSERT_EQ(40, rp->pid_);

  ret.reset();
  p1->pid_ = 43; //for accept
  p1->pentry_ = 89;
  memcpy(p1->data_, "writefail", 9);
  m1->type_ = kMsgType_ACCEPT_REQ;
  p1->status_ = kPaxosState_PROMISED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ACCEPTED_FAILED, rp->status_);
  ASSERT_EQ(43, rp->pid_);

  // test commit
  // test invalid commit pp
  ret.reset();
  p1->pid_ = 33; //
  p1->pentry_ = 118; // not existed entry
  memcpy(p1->data_, "miliao", 6);
  m1->type_ = kMsgType_CHOSEN_REQ;
  p1->status_ = kPaxosState_ACCEPTED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_INVALID_PROPOSAL, rp->status_);

  p1->pid_ = 43; // pid not equal
  p1->pentry_ = 88;
  m1->type_ = kMsgType_CHOSEN_REQ;
  p1->status_ = kPaxosState_ACCEPTED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_INVALID_PROPOSAL, rp->status_);

  p1->pid_ = 33;
  p1->pentry_ = 88;
  p1->value_id_++; // invalid value id
  m1->type_ = kMsgType_CHOSEN_REQ;
  p1->status_ = kPaxosState_ACCEPTED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_INVALID_PROPOSAL, rp->status_);

  // chosen write failed.
  p1->pid_ = 33;
  p1->pentry_ = 88;
  p1->value_id_--;
  m1->type_ = kMsgType_CHOSEN_REQ;
  p1->status_ = kPaxosState_ACCEPTED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_COMMIT_FAILED, rp->status_);

  // chosen succ
  p1->pid_ = 53; // bigger prepare id to change accepted value
  p1->pentry_ = 88;
  p1->size_ = 9;
  memcpy(p1->data_, "normalval", 9);
  m1->type_ = kMsgType_ACCEPT_REQ;
  p1->status_ = kPaxosState_PROMISED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ACCEPTED, rp->status_);
  ASSERT_EQ(53, rp->pid_);

  // test chosen succ
  p1->pid_ = 53;
  p1->pentry_ = 88;
  m1->type_ = kMsgType_CHOSEN_REQ;
  p1->status_ = kPaxosState_ACCEPTED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_CHOSEN, rp->status_);

  p1->pid_ = 53;
  p1->pentry_ = 88;
  m1->type_ = kMsgType_CHOSEN_REQ;
  p1->status_ = kPaxosState_ACCEPTED;
  acceptor.AddMsg(m1, verify);
  ASSERT_TRUE(wg1.Wait(30));
  rp = GetProposalFromMsg(ret.get());
  ASSERT_EQ(kPaxosState_ALREADY_CHOSEN, rp->status_);

  // TODO
}
