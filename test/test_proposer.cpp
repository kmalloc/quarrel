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

    if (req2->type_ == kMsgType_PREPARE_REQ && fake_rsp_) {
      fake_rsp_->from_ = addr_.id_;
      fake_rsp_->reqid_ = req2->reqid_;
      fake_rsp_->type_ = kMsgType_PREPARE_RSP;
      auto reqfp = GetProposalFromMsg(req2.get());
      auto rspfp = GetProposalFromMsg(fake_rsp_.get());
      rspfp->plid_ = reqfp->plid_;
      rspfp->pentry_ = reqfp->pentry_;
      rspfp->pid_ = reqfp->pid_ - addr_.id_;
      rspfp->value_id_ = reqfp->value_id_ + 1;
      rsp = std::move(fake_rsp_);
      LOG_INFO << "acceptor(" << addr_.id_
               << ") from local returns last vote, req pid:" << reqfp->pid_
               << ", rsp pid:" << rspfp->pid_ << ", req vsz:" << reqfp->size_
               << ", rsp vsz:" << rspfp->size_
               << ", req vid:" << rspfp->value_id_
               << ", rsp vid:" << rspfp->value_id_;
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
      promised_ = req2;
      rspfp->opaque_++;
      rspfp->status_ = kPaxosState_PROMISED;
    } else if (req2->type_ == kMsgType_ACCEPT_REQ) {
      accepted_ = req2;
      pp2->opaque_++;
      rspfp->opaque_++;
      rspfp->status_ = kPaxosState_ACCEPTED;
    } else if (req2->type_ == kMsgType_CHOSEN_REQ) {
      chosen_ = true;
      rspfp->last_chosen_ = rspfp->pentry_;
      rspfp->status_ = kPaxosState_CHOSEN;
      if (chosen_notify_) {
        chosen_notify_(req2);
      }
    }

    LOG_INFO << "acceptor(" << addr_.id_ << "), local conn returns rsp, type:" << rsp->type_
             << ", reqid:" << rspfp->pid_ << ", pinst:" << rspfp->plid_ << ", entry:" << rspfp->pentry_;

    data.cb_(std::move(rsp));
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
    auto rsper = [this](std::shared_ptr<PaxosMsg> msg) mutable {
      auto tm = std::chrono::milliseconds(1);
      std::this_thread::sleep_for(tm);

      this->HandleRecv(msg);
      auto pp = GetProposalFromMsg(msg.get());

      LOG_INFO << "acceptor(" << this->addr_.id_
               << ") dummy rsp from remoteConn, type: " << msg->type_
               << ",msg:(reqid-" << msg->reqid_ << ", opaque-" << pp->opaque_
               << ", vid-" << pp->value_id_ << ",vsz:" << pp->size_ << ", pid-"
               << pp->pid_ << ")@(" << pp->plid_ << ", " << pp->pentry_ << ")";
    };

    auto req2 = CloneProposalMsg(*req.get());
    auto rsp = CloneProposalMsg(*req.get());
    rsp->from_ = addr_.id_;
    rsp->type_ = req->type_ + 1;

    auto reqfp = GetProposalFromMsg(req2.get());

    if (req2->type_ == kMsgType_PREPARE_REQ && fake_rsp_) {
      fake_rsp_->from_ = addr_.id_;
      fake_rsp_->reqid_ = req2->reqid_;
      fake_rsp_->type_ = kMsgType_PREPARE_RSP;
      auto rspfp = GetProposalFromMsg(fake_rsp_.get());

      rspfp->plid_ = reqfp->plid_;
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
      LOG_ERR << "reject from remote";

      auto pp2 = GetProposalFromMsg(req2.get());
      rpp->pid_ = pp2->pid_ + 1;
      // rpp->status_ = kPaxosState_PROMISED_FAILED;
    } else if (rejectReadProbe_ && req2->type_ == kMsgType_PREPARE_REQ) {
      auto pp2 = GetProposalFromMsg(req2.get());
      rpp->pid_ = pp2->pid_ + 1;
    }

    if (req2->type_ == kMsgType_PREPARE_REQ) {
      rpp->opaque_++;
      promised_ = req2;
      rpp->status_ = kPaxosState_PROMISED;
    } else if (req2->type_ == kMsgType_ACCEPT_REQ) {
      rpp->opaque_++;
      reqfp->opaque_++;
      accepted_ = req2;
      rpp->status_ = kPaxosState_ACCEPTED;
    } else if (req2->type_ == kMsgType_CHOSEN_REQ) {
      chosen_ = true;
      rpp->status_ = kPaxosState_CHOSEN;
      rpp->last_chosen_ = rpp->pentry_;
      if (chosen_notify_) {
        chosen_notify_(req2);
      }
    }

    std::async(std::launch::async, rsper, std::move(rsp));
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
  config->timeout_ = 8;  // 8ms
  config->pid_cookie_ = 8; // prepare id > 8
  config->local_ = {0, ConnType_LOCAL, "xxxx:yyy"};
  config->plog_inst_num_ = 666;
  config->peer_.push_back({1, ConnType_REMOTE, "aaaa:bb"});
  config->peer_.push_back({2, ConnType_REMOTE, "aaaa2:bb2"});

  Proposer pp(config);

  auto config2 = std::make_shared<Configure>();
  config2->timeout_ = 8;     // 8ms
  config2->pid_cookie_ = 8;  // prepare id > 8
  config2->local_ = {2, ConnType_LOCAL, "xxxx:yyy"};
  config2->plog_inst_num_ = 666;
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
  ASSERT_EQ(2, conn_mng->CreateConn());

  auto& local = conn_mng->GetLocalConn();
  ASSERT_EQ(ConnType_LOCAL, local->GetType());
  ASSERT_STREQ("xxxx:yyy", local->GetAddr().addr_.c_str());

  auto& r1 = conn_mng->GetRemoteConn(0)[0];
  auto& r2 = conn_mng->GetRemoteConn(0)[1];

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

  dr1->fake_rsp_ = fake_rsp;

  LOG_INFO << "#########last vote test##########";

  ASSERT_EQ(kErrCode_PREPARE_PEER_VALUE, pp.Propose(0xbadf00d, "dummy value", 11));

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

  ASSERT_EQ(kErrCode_PREPARE_PEER_VALUE, pp.Propose(0xbadf00d, "dummy value", 22));

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
  ASSERT_EQ(kErrCode_ACCEPT_NOT_QUORAUM, pp.Propose(0xbadf00d, "dummy value", 19));

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
  ASSERT_EQ(kErrCode_ACCEPT_NOT_QUORAUM, pp.Propose(233, "dummy value", 2));
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

  auto chosen_notify1 = [&](std::shared_ptr<PaxosMsg> m) { if (m->from_ != config->local_.id_) pp.HandleChosenNotify(std::move(m));return 0; };
  auto chosen_notify2 = [&](std::shared_ptr<PaxosMsg> m) { if (m->from_ != config2->local_.id_) pps.HandleChosenNotify(std::move(m));return 0; };

  dr1->chosen_notify_ = chosen_notify1;
  dr2->chosen_notify_ = chosen_notify2;

  // test write from slave
  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "dummy value", 2));
  auto pp21 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto entry21 = pp21->pentry_;

  ASSERT_EQ(kErrCode_OK, pps.Propose(233, "dummy value", 2));
  auto pp22 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto entry22 = pp22->pentry_;
  ASSERT_EQ(233 + 2, pp22->opaque_);
  ASSERT_EQ(entry21 + 1, entry22);

  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "dummy value", 2));
  auto pp23 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto entry23 = pp23->pentry_;
  ASSERT_EQ(233 + 2, pp23->opaque_);
  ASSERT_EQ(entry22 + 1, entry23);

  ASSERT_EQ(kErrCode_OK, pp.Propose(233, "dummy value", 2));
  auto pp24 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
  auto entry24 = pp24->pentry_;
  ASSERT_EQ(233 + 1, pp24->opaque_);
  ASSERT_EQ(entry23 + 1, entry24);
}
