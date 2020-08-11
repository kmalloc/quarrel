#include "gtest/gtest.h"

#include <string>
#include <thread>
#include <chrono>
#include <future>

#include "logger.h"

#include "proposer.h"

using namespace quarrel;

struct DummyLocalConn: public LocalConn {
    public:
        DummyLocalConn(AddrInfo addr): LocalConn(std::move(addr)) {}

        virtual int DoRpcRequest(RpcReqData data) {
            auto rsp = CloneProposalMsg(*data.data_.get());
            auto req2 = CloneProposalMsg(*data.data_.get());
            rsp->from_ = addr_.id_;
            rsp->type_ = rsp->type_+1;

            if (req2->type_ == kMsgType_PREPARE_REQ && fake_rsp_) {
                fake_rsp_->from_ = addr_.id_;
                fake_rsp_->reqid_ = req2->reqid_;
                fake_rsp_->type_ = kMsgType_PREPARE_RSP;
                auto reqfp = GetProposalFromMsg(req2.get());
                auto rspfp = GetProposalFromMsg(fake_rsp_.get());
                rspfp->pid_ = reqfp->pid_ - addr_.id_;
                rspfp->value_id_ = reqfp->value_id_ + 1;

                rsp = std::move(fake_rsp_);
            }

            if ((rejectAccept_ && req2->type_ == kMsgType_ACCEPT_REQ) ||
                (rejectPrepare_ && req2->type_ == kMsgType_PREPARE_REQ)) {
              auto pp = GetProposalFromMsg(rsp.get());
              auto pp2 = GetProposalFromMsg(req2.get());

              pp->pid_ = pp2->pid_ + 1;
            }

            data.cb_(std::move(rsp));

            if (req2->type_ == kMsgType_PREPARE_REQ) {
                promised_ = req2;
            } else if (req2->type_ == kMsgType_ACCEPT_REQ) {
                accepted_ = req2;
            } else if (req2->type_ == kMsgType_CHOSEN_REQ) {
                chosen_ = true;
            }

            return 0;
        }

        bool chosen_{false};
        bool rejectPrepare_{false};
        bool rejectAccept_{false};
        std::shared_ptr<PaxosMsg> accepted_;
        std::shared_ptr<PaxosMsg> promised_;
        std::shared_ptr<PaxosMsg> fake_rsp_;
};

struct DummyRemoteConn: public RemoteConn {
    public:
        DummyRemoteConn(AddrInfo addr): RemoteConn(100, std::move(addr)) {}
        virtual ~DummyRemoteConn() {}

        virtual int DoWrite(std::shared_ptr<PaxosMsg> req) {
          auto rsper = [this](std::shared_ptr<PaxosMsg> msg) mutable {
            auto tm = std::chrono::milliseconds(1);
            std::this_thread::sleep_for(tm);

            this->HandleRecv(msg);
            auto pp = GetProposalFromMsg(msg.get());

            LOG_INFO << "acceptor(" << this->addr_.id_
                     << ") dummy call to HandleRecv(), type: " << msg->type_
                     << ",msg:(reqid-" << msg->reqid_ << ", opaque-"
                     << pp->opaque_ << ", vid-" << pp->value_id_
                     << ", pid-" << pp->pid_ << ")";
          };

          auto req2 = CloneProposalMsg(*req.get());
          auto rsp = CloneProposalMsg(*req.get());
          rsp->from_ = addr_.id_;
          rsp->type_ = req->type_+1;

          if (req2->type_ == kMsgType_PREPARE_REQ && fake_rsp_) {
            fake_rsp_->from_ = addr_.id_;
            fake_rsp_->reqid_ = req2->reqid_;
            fake_rsp_->type_ = kMsgType_PREPARE_RSP;
            auto reqfp = GetProposalFromMsg(req2.get());
            auto rspfp = GetProposalFromMsg(fake_rsp_.get());

            rspfp->pid_ = reqfp->pid_ - addr_.id_;
            rspfp->value_id_ = reqfp->value_id_ + 1;

            LOG_INFO << "set reqid for last vote rsp, req pid:" << reqfp->pid_
                     << ", rsp pid:" << rspfp->pid_;

                rsp = std::move(fake_rsp_);
          }

          if ((rejectAccept_ && req2->type_ == kMsgType_ACCEPT_REQ) ||
              (rejectPrepare_ && req2->type_ == kMsgType_PREPARE_REQ)) {

            LOG_ERR << "reject from remote";

            auto pp = GetProposalFromMsg(rsp.get());
            auto pp2 = GetProposalFromMsg(req2.get());
            pp->pid_ = pp2->pid_ + 1;
          }

          std::async(std::launch::async, rsper, std::move(rsp));

          LOG_INFO << "dummy call to DoWrite()";

          if (req2->type_ == kMsgType_PREPARE_REQ) {
            promised_ = req2;
          } else if (req2->type_ == kMsgType_ACCEPT_REQ) {
            accepted_ = req2;
          } else if (req2->type_ == kMsgType_CHOSEN_REQ) {
            chosen_ = true;
          }
          return kErrCode_OK;
        }

        bool chosen_{false};
        bool rejectPrepare_{false};
        bool rejectAccept_{false};
        std::shared_ptr<PaxosMsg> accepted_;
        std::shared_ptr<PaxosMsg> promised_;
        std::shared_ptr<PaxosMsg> fake_rsp_;
};

struct DummyEntryMng: public EntryMng {
    DummyEntryMng(std::shared_ptr<Configure> config, uint64_t pinst)
        :EntryMng(std::move(config), pinst) {}

    virtual int SaveEntry(uint64_t pinst, uint64_t entry, const Entry& ent) {
        (void)pinst;(void)entry;(void)ent;
        return kErrCode_OK;
    }
    virtual int LoadEntry(uint64_t pinst, uint64_t entry, Entry& ent) {
        (void)pinst;(void)entry;(void)ent;
        return kErrCode_OK;
    }
    virtual int Checkpoint(uint64_t pinst, uint64_t term) {
        (void)pinst;(void)term;
        return kErrCode_OK;
    }

    virtual uint64_t GetMaxCommittedEntry(uint64_t pinst) {
        (void)pinst;
        return max_committed_++%8;
    }

    virtual int LoadUncommittedEntry(std::vector<std::unique_ptr<Entry>>& entries) {
        (void)entries;
        return kErrCode_OK;
    }

    uint64_t max_committed_{0};
};

TEST(proposer, doPropose) {
    auto config = std::make_shared<Configure>();
    config->timeout_ = 8; // 8ms
    config->local_ = {1, ConnType_LOCAL, "xxxx:yyy"};
    config->local_id_ = 1;
    config->plog_inst_num_ = 5;
    config->total_acceptor_ = 3;
    config->peer_.push_back({2, ConnType_REMOTE, "aaaa:bb"});
    config->peer_.push_back({3, ConnType_REMOTE, "aaaa2:bb2"});

    auto conn_creator = [](AddrInfo addr) -> std::unique_ptr<Conn> {
        if (addr.type_ == ConnType_LOCAL) return std::unique_ptr<DummyLocalConn>(new DummyLocalConn(std::move(addr)));

        return std::unique_ptr<DummyRemoteConn>(new DummyRemoteConn(std::move(addr)));
    };

    auto conn_mng = std::make_shared<ConnMng>(config);

    conn_mng->SetConnCreator(conn_creator);
    ASSERT_EQ(3, conn_mng->CreateConn());

    auto& local = conn_mng->GetLocalConn();
    ASSERT_EQ(ConnType_LOCAL, local->GetType());
    ASSERT_STREQ("xxxx:yyy", local->GetAddr().addr_.c_str());

    auto& r1 = conn_mng->GetRemoteConn()[0];
    auto& r2 = conn_mng->GetRemoteConn()[1];

    ASSERT_EQ(ConnType_REMOTE, r1->GetType());
    ASSERT_STREQ("aaaa:bb", r1->GetAddr().addr_.c_str());
    ASSERT_EQ(ConnType_REMOTE, r2->GetType());
    ASSERT_STREQ("aaaa2:bb2", r2->GetAddr().addr_.c_str());

    Proposer pp(config);
    std::shared_ptr<PlogMng> pmn = std::make_shared<PlogMng>(config);

    auto entry_mng_creator = [](int pinst, std::shared_ptr<Configure> conf) -> std::unique_ptr<EntryMng> {
        return std::unique_ptr<EntryMng>(new DummyEntryMng(std::move(conf), pinst));
    };

    pmn->SetEntryMngCreator(entry_mng_creator);
    pmn->InitPlog();

    pp.SetPlogMng(pmn);
    pp.SetConnMng(conn_mng);

    auto dr1 = dynamic_cast<DummyRemoteConn*>(r1.get());
    auto dr2 = dynamic_cast<DummyRemoteConn*>(r2.get());
    auto dlocal = dynamic_cast<DummyLocalConn*>(local.get());

    ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));
    ASSERT_EQ(config->local_id_, dr1->accepted_->from_);
    ASSERT_EQ(kMsgType_ACCEPT_REQ, dr1->accepted_->type_);
    auto p11 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
    auto p12 = reinterpret_cast<Proposal*>(dr2->accepted_->data_);
    auto p13 = reinterpret_cast<Proposal*>(dlocal->accepted_->data_);
    ASSERT_EQ(config->local_id_, p11->proposer_);
    ASSERT_EQ(kPaxosState_ACCEPTED, p11->status_);
    ASSERT_EQ(11, p11->size_);
    ASSERT_EQ(0xbadf00d, p11->opaque_);
    ASSERT_EQ(0, memcmp("dummy value", p11->data_, 11));
    ASSERT_EQ(0, memcmp(p11, p12, ProposalHeaderSz + p11->size_));
    ASSERT_EQ(0, memcmp(p11, p13, ProposalHeaderSz + p11->size_));
    ASSERT_TRUE(dr1->chosen_);
    ASSERT_TRUE(dr2->chosen_);
    ASSERT_TRUE(dlocal->chosen_);

    ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));
    ASSERT_EQ(config->local_id_, dr1->accepted_->from_);
    ASSERT_EQ(kMsgType_ACCEPT_REQ, dr1->accepted_->type_);
    auto p21 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
    auto p22 = reinterpret_cast<Proposal*>(dr2->accepted_->data_);
    auto p23 = reinterpret_cast<Proposal*>(dlocal->accepted_->data_);
    ASSERT_EQ(config->local_id_, p21->proposer_);
    ASSERT_EQ(kPaxosState_ACCEPTED, p21->status_);
    ASSERT_EQ(11, p21->size_);
    ASSERT_EQ(0xbadf00d, p21->opaque_);
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

    for (auto i = 0 ;i < 16; i++) {
        pmn->SetPrepareIdGreaterThan(0, i, 8);
    }

    ASSERT_EQ(kErrCode_PREPARE_PEER_VALUE, pp.Propose(0xbadf00d, "dummy value"));

    ASSERT_EQ(1, dr1->accepted_->from_);
    ASSERT_EQ(1, dr2->accepted_->from_);
    ASSERT_EQ(1, dlocal->accepted_->from_);
    ASSERT_EQ(kMsgType_ACCEPT_REQ, dr1->accepted_->type_);
    ASSERT_EQ(kMsgType_ACCEPT_REQ, dr2->accepted_->type_);
    ASSERT_EQ(kMsgType_ACCEPT_REQ, dlocal->accepted_->type_);

    auto p31 = reinterpret_cast<Proposal*>(dr1->accepted_->data_);
    auto p32 = reinterpret_cast<Proposal*>(dr2->accepted_->data_);
    auto p33 = reinterpret_cast<Proposal*>(dlocal->accepted_->data_);
    ASSERT_EQ(2, p31->proposer_);
    ASSERT_EQ(2, p32->proposer_);
    ASSERT_EQ(2, p33->proposer_);
    ASSERT_EQ(kPaxosState_ACCEPTED, p31->status_);
    ASSERT_EQ(kPaxosState_ACCEPTED, p32->status_);
    ASSERT_EQ(kPaxosState_ACCEPTED, p33->status_);
    ASSERT_EQ(12, p31->size_);
    ASSERT_EQ(12, p32->size_);
    ASSERT_EQ(12, p33->size_);
    ASSERT_EQ(0xbadf00d+23, p31->opaque_);
    ASSERT_EQ(0xbadf00d+23, p32->opaque_);
    ASSERT_EQ(0xbadf00d+23, p33->opaque_);
    ASSERT_EQ(0, memcmp("miliao dummy", p31->data_, 12));
    ASSERT_EQ(0, memcmp(p31, p32, ProposalHeaderSz + p31->size_));
    ASSERT_EQ(0, memcmp(p31, p33, ProposalHeaderSz + p31->size_));

    auto fake_rsp2 = CloneProposalMsg(*fake_rsp);
    auto pp2 = reinterpret_cast<Proposal*>(fake_rsp2->data_);
    pp2->proposer_ = 3;
    pp2->opaque_ = 0xbadf00d+43;
    strncpy(reinterpret_cast<char*>(pp2->data_), "mi1ia0 dummy", pp2->size_);

    dr1->fake_rsp_ = fake_rsp2;
    dr2->fake_rsp_ = fake_rsp;

    LOG_INFO << "@@@@@@test multiple last vote@@@@@";
    ASSERT_EQ(kErrCode_PREPARE_PEER_VALUE, pp.Propose(0xbadf00d, "dummy value"));

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
    ASSERT_EQ(0xbadf00d+43, p41->opaque_);
    ASSERT_EQ(0xbadf00d+43, p42->opaque_);
    ASSERT_EQ(0xbadf00d+43, p43->opaque_);
    ASSERT_EQ(0, memcmp("mi1ia0 dummy", p41->data_, 12));
    ASSERT_EQ(0, memcmp(p41, p42, ProposalHeaderSz + p41->size_));
    ASSERT_EQ(0, memcmp(p41, p43, ProposalHeaderSz + p41->size_));

    LOG_INFO << "@@@@@@test reject@@@@@";
    dr1->fake_rsp_.reset();
    dr2->fake_rsp_.reset();
    dlocal->fake_rsp_.reset();

    dr1->rejectPrepare_ = true;
    ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));

    dr1->rejectPrepare_ = true;
    dr2->rejectPrepare_ = true;
    ASSERT_EQ(kErrCode_PREPARE_NOT_QUORAUM, pp.Propose(0xbadf00d, "dummy value"));

    dr1->rejectPrepare_ = false;
    dr2->rejectPrepare_ = false;
    ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));

    dr2->rejectAccept_ = true;
    ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));

    dr1->rejectPrepare_ = true;
    dr2->rejectAccept_ = true;
    ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));

    dr1->rejectPrepare_ = false;
    dr1->rejectAccept_ = false;

    dr2->rejectPrepare_ = true;
    dr2->rejectAccept_ = true;
    ASSERT_EQ(kErrCode_OK, pp.Propose(0xbadf00d, "dummy value"));

    dr1->rejectAccept_ = true;
    dr1->rejectPrepare_ = false;
    dr2->rejectAccept_ = true;
    dr2->rejectPrepare_ = false;

    LOG_INFO << "test accept reject";
    ASSERT_EQ(kErrCode_ACCEPT_NOT_QUORAUM, pp.Propose(0xbadf00d, "dummy value"));

    // multiple entry test.
}
