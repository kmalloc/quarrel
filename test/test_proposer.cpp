#include "gtest/gtest.h"

#include <string>
#include <thread>
#include <chrono>
#include <future>

#include "plog.h"
#include "logger.h"
#include "paxos_group.h"

#include "proposer.h"

using namespace quarrel;

struct DummyLocalConn : public LocalConn {
 public:
  explicit DummyLocalConn(AddrInfo addr)
      : LocalConn(std::move(addr)) {}

  virtual int DoRpcRequest(RpcReqData data) {
    auto rsp = CloneProposalMsg(*data.data_.get());
    auto req2 = CloneProposalMsg(*data.data_.get());
    rsp->from_ = addr_.id_;
    rsp->type_ = rsp->type_ + 1;
    auto reqpp = GetProposalFromMsg(req2.get());

    if (reqpp->pid_ > 0 && req2->type_ == kMsgType_PREPARE_REQ && fake_rsp_) {
      fake_rsp_->from_ = addr_.id_;
      fake_rsp_->reqid_ = req2->reqid_;
      fake_rsp_->type_ = kMsgType_PREPARE_RSP;
      auto reqfp = GetProposalFromMsg(req2.get());
      auto rspfp = GetProposalFromMsg(fake_rsp_.get());
      rspfp->pinst_ = reqfp->pinst_;
      rspfp->pentry_ = reqfp->pentry_;
      rspfp->pid_ = reqfp->pid_ - 1;
      rspfp->value_id_ = reqfp->value_id_ + 1;
      rsp = std::move(fake_rsp_);
      LOG_INFO << "acceptor(" << addr_.id_
               << ") from local returns last vote, req pid:" << reqfp->pid_
               << ", rsp pid:" << rspfp->pid_ << ", req vsz:" << reqfp->size_
               << ", rsp vsz:" << rspfp->size_
               << ", req vid:" << rspfp->value_id_
               << ", rsp vid:" << rspfp->value_id_
               << ", @(" << rspfp->pinst_ << "," << rspfp->pentry_ << "), proposer:"
               << rspfp->proposer_;
    }

    auto pp2 = GetProposalFromMsg(req2.get());

    if ((rejectAccept_ && req2->type_ == kMsgType_ACCEPT_REQ) ||
        (rejectPrepare_ && req2->type_ == kMsgType_PREPARE_REQ)) {
      auto pp = GetProposalFromMsg(rsp.get());
      pp->pid_ = pp2->pid_ + 1;
    } else if (rejectReadProbe_ && req2->type_ == kMsgType_PREPARE_REQ) {
      auto pp = GetProposalFromMsg(rsp.get());
      pp->pid_ = pp2->pid_ + 1;
    }

    auto rspfp = GetProposalFromMsg(rsp.get());

    if (req2->type_ == kMsgType_PREPARE_REQ) {
      chosen_ = false;
      promised_ = req2;
      rspfp->opaque_++;
      rspfp->status_ = kPaxosState_PROMISED;
    } else if (req2->type_ == kMsgType_ACCEPT_REQ) {
      chosen_ = false;
      accepted_ = req2;
      pp2->opaque_++;
      rspfp->opaque_++;
      rspfp->status_ = kPaxosState_ACCEPTED;
    } else if (req2->type_ == kMsgType_CHOSEN_REQ) {
      chosen_ = true;
      rspfp->last_chosen_ = rspfp->pentry_;
      rspfp->status_ = kPaxosState_CHOSEN;
    } else {
      chosen_ = false;
    }

    LOG_INFO << "acceptor(" << addr_.id_ << "), local conn returns rsp, type:" << rsp->type_
             << ", rsp opaque:" << rspfp->opaque_ << ", reqid:" << rspfp->pid_ << ", @("
             << rspfp->pinst_ << "," << rspfp->pentry_ << "), req proposer:" << rspfp->proposer_;

    data.cb_(std::move(rsp));

    if (chosen_ && chosen_notify_) {
      std::thread{[=]() { chosen_notify_(req2); }}.detach();
    }
    return 0;
  }

  bool chosen_{false};
  bool rejectPrepare_{false};
  bool rejectAccept_{false};
  bool rejectReadProbe_{false};

  std::shared_ptr<PaxosMsg> accepted_;
  std::shared_ptr<PaxosMsg> promised_;
  std::shared_ptr<PaxosMsg> fake_rsp_;

  std::function<int(std::shared_ptr<PaxosMsg>)> chosen_notify_;
};

struct DummyRemoteConn : public RemoteConn {
 public:
  explicit DummyRemoteConn(AddrInfo addr)
      : RemoteConn(100, std::move(addr)) {}

  virtual ~DummyRemoteConn() {}

  virtual int DoWrite(std::shared_ptr<PaxosMsg> req) {
    auto rsper = [this](std::shared_ptr<PaxosMsg> msg, bool chosen) mutable {
      auto tm = std::chrono::microseconds(10);
      std::this_thread::sleep_for(tm);

      auto pp = GetProposalFromMsg(msg.get());
      LOG_INFO << "acceptor(" << this->addr_.id_
               << ") dummy rsp from remoteConn, type: " << msg->type_
               << ",msg:(reqid-" << msg->reqid_ << ", opaque-" << pp->opaque_
               << ", vid-" << pp->value_id_ << ",vsz:" << pp->size_ << ", pid-"
               << pp->pid_ << ")@(" << pp->pinst_ << ", " << pp->pentry_ << "), req proposer:" << pp->proposer_;

      auto msg2 = CloneProposalMsg(*msg);
      this->HandleRecv(std::move(msg2));

      if (chosen && chosen_notify_ && ((pp->opaque_ / 10) % 1000) == 233) {
        chosen_notify_(msg);
      }
    };

    auto req2 = CloneProposalMsg(*req.get());
    auto rsp = CloneProposalMsg(*req.get());

    rsp->from_ = addr_.id_;
    rsp->type_ = req->type_ + 1;

    auto pp = GetProposalFromMsg(req.get());

    LOG_INFO << "acceptor(" << this->addr_.id_
             << ") dummy remoteConn DoWrite, type: " << req->type_
             << ",msg:(reqid-" << req->reqid_ << ", opaque-" << pp->opaque_
             << ", vid-" << pp->value_id_ << ",vsz:" << pp->size_ << ", pid-"
             << pp->pid_ << ")@(" << pp->pinst_ << ", " << pp->pentry_ << "), req proposer:" << pp->proposer_;

    auto reqfp = GetProposalFromMsg(req2.get());

    if (reqfp->pid_ > 0 && req2->type_ == kMsgType_PREPARE_REQ && fake_rsp_) {
      fake_rsp_->from_ = addr_.id_;
      fake_rsp_->reqid_ = req2->reqid_;
      fake_rsp_->type_ = kMsgType_PREPARE_RSP;
      auto rspfp = GetProposalFromMsg(fake_rsp_.get());

      rspfp->pinst_ = reqfp->pinst_;
      rspfp->pentry_ = reqfp->pentry_;

      rspfp->pid_ = reqfp->pid_ - addr_.id_;  // simulate returning last vote
      rspfp->value_id_ = reqfp->value_id_ + 1;

      LOG_INFO << "acceptor(" << addr_.id_
               << ") from remote returns last vote, req pid:" << reqfp->pid_
               << ", rsp pid:" << rspfp->pid_ << ", req vsz:" << reqfp->size_
               << ", rsp vsz:" << rspfp->size_
               << ", req vid:" << reqfp->value_id_
               << ", rsp vid:" << rspfp->value_id_;

      rsp = std::move(fake_rsp_);
    }

    auto rpp = GetProposalFromMsg(rsp.get());
    if ((rejectAccept_ && req2->type_ == kMsgType_ACCEPT_REQ) ||
        (rejectPrepare_ && req2->type_ == kMsgType_PREPARE_REQ)) {
      LOG_ERR << "acceptor(" << addr_.id_ << ")@(" << rpp->pinst_ << ","
              << rpp->pentry_ << ") rejected from remote";

      auto pp2 = GetProposalFromMsg(req2.get());
      rpp->pid_ = pp2->pid_ + 1;
      // rpp->status_ = kPaxosState_PROMISED_FAILED;
    } else if (rejectReadProbe_ && req2->type_ == kMsgType_PREPARE_REQ) {
      auto pp2 = GetProposalFromMsg(req2.get());
      rpp->pid_ = pp2->pid_ + 1;
    }

    if (req2->type_ == kMsgType_PREPARE_REQ) {
      chosen_ = false;
      rpp->opaque_++;
      promised_ = req2;
      rpp->status_ = kPaxosState_PROMISED;
    } else if (req2->type_ == kMsgType_ACCEPT_REQ) {
      chosen_ = false;
      rpp->opaque_++;
      reqfp->opaque_++;
      accepted_ = req2;
      rpp->status_ = kPaxosState_ACCEPTED;
    } else if (req2->type_ == kMsgType_CHOSEN_REQ) {
      chosen_ = true;
      rpp->status_ = kPaxosState_CHOSEN;
      rpp->last_chosen_ = rpp->pentry_;
    } else {
      chosen_ = false;
    }

    // async will block
    // std::async(std::launch::async, rsper, std::move(rsp));
    std::thread{rsper, std::move(rsp), chosen_}.detach();

    return kErrCode_OK;
  }

  bool chosen_{false};
  bool rejectPrepare_{false};
  bool rejectAccept_{false};
  bool rejectReadProbe_{false};

  std::shared_ptr<PaxosMsg> accepted_;
  std::shared_ptr<PaxosMsg> promised_;
  std::shared_ptr<PaxosMsg> fake_rsp_;
  std::function<int(std::shared_ptr<PaxosMsg>)> chosen_notify_;
};

TEST(proposer, doPropose) {
  auto config = std::make_shared<Configure>();
  config->timeout_ = 18;  // 8ms
  config->local_ = {0, ConnType_LOCAL, "xxxx:yyy"};
  config->plog_inst_num_ = 666;
  config->peer_.push_back(config->local_);
  config->peer_.push_back({1, ConnType_REMOTE, "aaaa:bb"});
  config->peer_.push_back({2, ConnType_REMOTE, "aaaa2:bb2"});

  Proposer pp(config);

  auto config2 = std::make_shared<Configure>();
  config2->timeout_ = 18;  // 8ms
  config2->local_ = {2, ConnType_LOCAL, "xxxx:yyy"};
  config2->plog_inst_num_ = 666;
  config2->peer_.push_back(config->local_);
  config2->peer_.push_back({1, ConnType_REMOTE, "aaaa:bb"});
  config2->peer_.push_back({0, ConnType_REMOTE, "aaaa2:bb2"});
  Proposer pps(config2);

  auto mapper = std::make_shared<PaxosGroup3>();

  auto conn_creator = [&](AddrInfo addr) -> std::unique_ptr<Conn> {
    LOG_INFO << "creating conn, type:" << addr.type_;
    if (addr.type_ == ConnType_LOCAL) {
      return make_unique<DummyLocalConn>(std::move(addr));
    }

    return make_unique<DummyRemoteConn>(std::move(addr));
  };

  auto conn_mng = std::make_shared<ConnMng>(config, mapper);

  conn_mng->SetConnCreator(conn_creator);
  ASSERT_EQ(3, conn_mng->CreateConn());

  auto& all_conn = conn_mng->GetAllConn(0);

  auto& r1 = all_conn[1];
  auto& r2 = all_conn[2];
  auto& local = all_conn[0];

  ASSERT_EQ(ConnType_LOCAL, local->GetType());
  ASSERT_STREQ("xxxx:yyy", local->GetAddr().addr_.c_str());

  ASSERT_EQ(ConnType_REMOTE, r1->GetType());
  ASSERT_STREQ("aaaa:bb", r1->GetAddr().addr_.c_str());
  ASSERT_EQ(ConnType_REMOTE, r2->GetType());
  ASSERT_STREQ("aaaa2:bb2", r2->GetAddr().addr_.c_str());

  pp.SetConnMng(conn_mng);
  pps.SetConnMng(conn_mng);

  auto mp = PaxosGroupBase::CreateGroup(3);

  auto dr1 = dynamic_cast<DummyRemoteConn*>(r1.get());
  auto dr2 = dynamic_cast<DummyRemoteConn*>(r2.get());
  auto dlocal = dynamic_cast<DummyLocalConn*>(local.get());

  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));
  ASSERT_EQ(config->local_.id_, dr1->accepted_->from_);
  ASSERT_EQ(kMsgType_ACCEPT_REQ, dr1->accepted_->type_);

  auto p11 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto p12 = reinterpret_cast<Proposal*>(dr2->accepted_->data_);
  auto p13 = reinterpret_cast<Proposal*>(dlocal->accepted_->data_);

  ASSERT_EQ(1, p11->pentry_);
  ASSERT_EQ(kPaxosState_ACCEPTED, p11->status_);
  ASSERT_EQ(mp->GetMemberIdBySvrId(0, config->local_.id_), p11->proposer_);
  ASSERT_EQ(11, p11->size_);
  ASSERT_EQ(0xbadf00d + 2, p11->opaque_);
  ASSERT_EQ(0, memcmp("dummy value", p11->data_, 11));
  ASSERT_EQ(0, memcmp(p11, p12, ProposalHeaderSz + p11->size_));
  ASSERT_EQ(0, memcmp(p11, p13, ProposalHeaderSz + p11->size_));
  ASSERT_TRUE(dr1->chosen_);
  ASSERT_TRUE(dr2->chosen_);
  ASSERT_TRUE(dlocal->chosen_);

  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));
  ASSERT_EQ(config->local_.id_, dr1->accepted_->from_);
  ASSERT_EQ(kMsgType_ACCEPT_REQ, dr1->accepted_->type_);

  auto p21 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto p22 = reinterpret_cast<Proposal*>(dr2->accepted_->data_);
  auto p23 = reinterpret_cast<Proposal*>(dlocal->accepted_->data_);

  ASSERT_EQ(2, p21->pentry_);
  ASSERT_EQ(kPaxosState_ACCEPTED, p21->status_);
  ASSERT_EQ(mp->GetMemberIdBySvrId(0, config->local_.id_), p21->proposer_);
  ASSERT_EQ(11, p21->size_);
  ASSERT_EQ(0xbadf00d + 1, p21->opaque_);
  ASSERT_EQ(0, memcmp("dummy value", p21->data_, 11));
  ASSERT_EQ(0, memcmp(p21, p22, ProposalHeaderSz + p21->size_));
  ASSERT_EQ(0, memcmp(p21, p23, ProposalHeaderSz + p21->size_));

  auto fake_rsp = AllocProposalMsg(12);
  fake_rsp->type_ = kMsgType_PREPARE_RSP;

  auto fp = reinterpret_cast<Proposal*>(fake_rsp->data_);
  fp->proposer_ = 2;
  fp->opaque_ = 0xbadf00d + 23;
  fp->status_ = kPaxosState_PROMISED;
  memcpy(fp->data_, "miliao dummy", 12);

  // must return by local conn.
  // since local conn is guranteed to reponse faster than remote conn.
  // and thus guarantee that last vote will come first.
  dlocal->fake_rsp_ = fake_rsp;

  dr1->accepted_.reset();
  dr2->accepted_.reset();
  dlocal->accepted_.reset();

  LOG_INFO << "#########last vote test##########";

  ASSERT_EQ(kErrCode_PREPARE_PEER_VALUE, pp.Propose(0xbadf00d, "dummy value", 11));

  // prepare peer value returns early,
  while (kErrCode_WORKING_IN_PROPGRESS == pp.Propose(1111111, "", 11)) continue;

  LOG_INFO << "read probe ret:" << pp.Propose(222222, "", 11);

  ASSERT_EQ(0, dr1->accepted_->from_);
  ASSERT_EQ(0, dr2->accepted_->from_);
  ASSERT_EQ(0, dlocal->accepted_->from_);
  ASSERT_EQ(kMsgType_ACCEPT_REQ, dr1->accepted_->type_);
  ASSERT_EQ(kMsgType_ACCEPT_REQ, dr2->accepted_->type_);
  ASSERT_EQ(kMsgType_ACCEPT_REQ, dlocal->accepted_->type_);

  auto p31 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto p32 = reinterpret_cast<Proposal*>(dr2->accepted_->data_);
  auto p33 = reinterpret_cast<Proposal*>(dlocal->accepted_->data_);

  ASSERT_EQ(1, p31->pentry_);
  ASSERT_EQ(2, p31->proposer_);
  ASSERT_EQ(2, p32->proposer_);
  ASSERT_EQ(2, p33->proposer_);
  ASSERT_EQ(kPaxosState_ACCEPTED, p31->status_);
  ASSERT_EQ(kPaxosState_ACCEPTED, p32->status_);
  ASSERT_EQ(kPaxosState_ACCEPTED, p33->status_);
  ASSERT_EQ(12, p31->size_);
  ASSERT_EQ(12, p32->size_);
  ASSERT_EQ(12, p33->size_);
  ASSERT_EQ(0xbadf00d + 23 + 2, p31->opaque_);
  ASSERT_EQ(0xbadf00d + 23 + 2, p32->opaque_);
  ASSERT_EQ(0xbadf00d + 23 + 2, p33->opaque_);
  ASSERT_EQ(0, memcmp("miliao dummy", p31->data_, 12));
  ASSERT_EQ(0, memcmp(p31, p32, ProposalHeaderSz + p31->size_));
  ASSERT_EQ(0, memcmp(p31, p33, ProposalHeaderSz + p31->size_));

  fake_rsp = AllocProposalMsg(12);
  auto fake_rsp2 = AllocProposalMsg(12);
  auto pp1 = reinterpret_cast<Proposal*>(fake_rsp->data_);
  auto pp2 = reinterpret_cast<Proposal*>(fake_rsp2->data_);
  pp1->proposer_ = 2;
  pp1->opaque_ = 0xbadf00d + 23;
  pp1->status_ = kPaxosState_PROMISED;
  memcpy(pp1->data_, "miliao dummy", pp1->size_);
  pp2->proposer_ = 3;
  pp2->opaque_ = 0xbadf00d + 43;
  pp2->status_ = kPaxosState_PROMISED;
  strncpy(reinterpret_cast<char*>(pp2->data_), "mi1ia0 dummy", pp2->size_);

  dr1->fake_rsp_ = fake_rsp2;
  dr2->fake_rsp_ = fake_rsp;

  LOG_INFO << "@@@@@@test multiple last vote@@@@@";

  config->pid_cookie_ = 8;  // prepare id > 8
  ASSERT_EQ(kErrCode_PREPARE_PEER_VALUE, pp.Propose(0xbadf00d, "dummy value", 22));

  while (kErrCode_WORKING_IN_PROPGRESS == pp.Propose(1111111, "", 22)) continue;

  config->pid_cookie_ = 0;

  auto p41 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto p42 = reinterpret_cast<Proposal*>(dr2->accepted_->data_);
  auto p43 = reinterpret_cast<Proposal*>(dlocal->accepted_->data_);

  ASSERT_EQ(3, p41->proposer_);
  ASSERT_EQ(3, p42->proposer_);
  ASSERT_EQ(3, p43->proposer_);
  ASSERT_EQ(kPaxosState_ACCEPTED, p41->status_);
  ASSERT_EQ(kPaxosState_ACCEPTED, p42->status_);
  ASSERT_EQ(kPaxosState_ACCEPTED, p43->status_);
  ASSERT_EQ(12, p41->size_);
  ASSERT_EQ(12, p42->size_);
  ASSERT_EQ(12, p43->size_);
  ASSERT_EQ(0xbadf00d + 43 + 2, p41->opaque_);
  ASSERT_EQ(0xbadf00d + 43 + 2, p42->opaque_);
  ASSERT_EQ(0xbadf00d + 43 + 2, p43->opaque_);
  ASSERT_EQ(0, memcmp("mi1ia0 dummy", p41->data_, 12));
  ASSERT_EQ(0, memcmp(p41, p42, ProposalHeaderSz + p41->size_));
  ASSERT_EQ(0, memcmp(p41, p43, ProposalHeaderSz + p41->size_));

  LOG_INFO << "@@@@@@test reject@@@@@";
  dr1->fake_rsp_.reset();
  dr2->fake_rsp_.reset();
  dlocal->fake_rsp_.reset();

  dr1->rejectPrepare_ = true;
  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value", 13));

  dr1->rejectPrepare_ = true;
  dr2->rejectPrepare_ = true;
  ASSERT_EQ(kErrCode_PREPARE_NOT_QUORAUM, pp.Propose(0xbadf00d, "dummy value", 14));

  dr1->rejectPrepare_ = false;
  dr2->rejectPrepare_ = false;
  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value", 15));

  dr2->rejectAccept_ = true;
  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value", 16));

  dr1->rejectPrepare_ = true;
  dr2->rejectAccept_ = true;
  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value", 17));

  dr1->rejectPrepare_ = false;
  dr1->rejectAccept_ = false;

  dr2->rejectPrepare_ = true;
  dr2->rejectAccept_ = true;
  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value", 18));

  dr1->rejectAccept_ = true;
  dr1->rejectPrepare_ = false;
  dr2->rejectAccept_ = true;
  dr2->rejectPrepare_ = false;

  LOG_INFO << "test accept reject";
  ASSERT_EQ(kErrCode_PEER_ACCEPT_REJECTED, pp.Propose(0xbadf00d, "dummy value", 19));

  dr1->rejectAccept_ = false;
  dr1->rejectPrepare_ = false;
  dr2->rejectAccept_ = false;
  dr2->rejectPrepare_ = false;

  // test proposer state

  LOG_INFO << "testing proposer state handling";
  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));

  auto pp11 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto prev_entry = pp11->pentry_;
  ASSERT_GT(prev_entry, 0);
  ASSERT_EQ(prev_entry - 1, pp11->last_chosen_);
  ASSERT_EQ(0, pp11->last_chosen_from_);

  ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));

  auto pp12 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto prev_entry2 = pp12->pentry_;

  ASSERT_EQ(prev_entry + 1, prev_entry2);
  ASSERT_EQ(prev_entry, pp12->last_chosen_);
  ASSERT_EQ(0, pp12->last_chosen_from_);
  ASSERT_EQ(1, pp12->pid_);

  // two phase
  LOG_INFO << "test two phase paxos";
  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "dummy value", 2));
  auto pp13 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  ASSERT_EQ(1, pp13->pentry_);
  ASSERT_EQ(233 + 2, pp13->opaque_);
  ASSERT_EQ(uint16_t(~0u), pp13->last_chosen_from_);

  ASSERT_GT(pp13->pid_, 1);
  ASSERT_EQ(mp->GetMemberIdBySvrId(2, config->local_.id_) + 1, pp13->pid_);

  // one phase
  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "dummy value", 2));
  auto pp14 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  ASSERT_EQ(1 + pp13->pentry_, pp14->pentry_);
  ASSERT_EQ(233 + 1, pp14->opaque_);

  // reject one phase accept
  dr1->rejectAccept_ = true;
  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "dummy value", 2));
  auto pp15 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  ASSERT_EQ(1 + pp14->pentry_, pp15->pentry_);
  ASSERT_EQ(233 + 1, pp15->opaque_);

  dr1->rejectAccept_ = true;
  dr2->rejectAccept_ = true;
  ASSERT_EQ(kErrCode_PEER_ACCEPT_REJECTED, pp.Propose(233, "dummy value", 2));
  auto pp16 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  ASSERT_EQ(1 + pp15->pentry_, pp16->pentry_);
  ASSERT_EQ(233 + 1, pp16->opaque_);

    //  two phase
  dr1->rejectAccept_ = false;
  dr2->rejectAccept_ = false;
  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "dummy value", 2));
  auto pp17 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  ASSERT_EQ(pp16->pentry_, pp17->pentry_);
  ASSERT_EQ(233 + 2, pp17->opaque_);

  // test read probe

  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "", 2));
  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "dummy value", 2));

  dr1->rejectReadProbe_ = true;
  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "", 2));
  dr2->rejectReadProbe_ = true;
  ASSERT_EQ(kErrCode_PREPARE_NOT_QUORAUM, pp.Propose(233, "", 2));

  dr1->rejectReadProbe_ = false;
  dr2->rejectReadProbe_ = false;

  WaitGroup wg(2);

  auto chosen_notify1 = [&](std::shared_ptr<PaxosMsg> m) { pp.OnEntryChosen(m); wg.Notify(); return 0; };
  auto chosen_notify2 = [&](std::shared_ptr<PaxosMsg> m) { pps.OnEntryChosen(m); wg.Notify();return 0; };

  dr1->chosen_notify_ = chosen_notify1;
  dr2->chosen_notify_ = chosen_notify2;

  // test write from slave
  LOG_INFO << "test write from save, starting by launching write from master(pp), then slave write(pps)";

  ASSERT_EQ(kErrCode_OK, pp.Propose(662333, "chosennotify", 2));  // write from master
  auto pp21 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto entry21 = pp21->pentry_;
  wg.Wait(30000);

  ASSERT_EQ(kErrCode_OK, pps.Propose(772333, "chosennotify", 2));  // write from slave
  auto pp22 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto entry22 = pp22->pentry_;
  ASSERT_EQ(772333 + 2, pp22->opaque_);
  ASSERT_EQ(entry21 + 1, entry22);
  wg.Wait(30000);

  ASSERT_EQ(kErrCode_OK, pp.Propose(882333, "chosennotify", 2));
  auto pp23 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto entry23 = pp23->pentry_;
  ASSERT_EQ(882333 + 2, pp23->opaque_);
  ASSERT_EQ(entry22 + 1, entry23);
  wg.Wait(30000);

  ASSERT_EQ(kErrCode_OK, pp.Propose(992333, "chosennotify", 2));
  auto pp24 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto entry24 = pp24->pentry_;
  ASSERT_EQ(992333 + 1, pp24->opaque_);
  ASSERT_EQ(entry23 + 1, entry24);
  wg.Wait(30000);

  LOG_ERR << "#################done testing############################";
}
