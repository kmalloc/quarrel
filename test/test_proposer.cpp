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

            if (req2->type_ == kMsgType_PREPARE_REQ && fake_rsp_) {
                fake_rsp_->from_ = addr_.id_;
                fake_rsp_->reqid_ = req2->reqid_;
                fake_rsp_->type_ = kMsgType_PREPARE_RSP;
                rsp = std::move(fake_rsp_);
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
        std::shared_ptr<PaxosMsg> accepted_;
        std::shared_ptr<PaxosMsg> promised_;
        std::shared_ptr<PaxosMsg> fake_rsp_;
};

struct DummyRemoteConn: public RemoteConn {
    public:
        DummyRemoteConn(AddrInfo addr): RemoteConn(100, std::move(addr)) {}
        virtual ~DummyRemoteConn() {}

        virtual int DoWrite(std::shared_ptr<PaxosMsg> req) {
          auto rsper = [self = this](std::shared_ptr<PaxosMsg> msg) mutable {
            auto tm = std::chrono::milliseconds(1);
            std::this_thread::sleep_for(tm);
            self->HandleRecv(msg);
            LOG_INFO << "dummy call to HandleRecv()";
          };

          auto req2 = CloneProposalMsg(*req.get());
          auto rsp = std::move(req);

          if (req2->type_ == kMsgType_PREPARE_REQ && fake_rsp_) {
            fake_rsp_->from_ = addr_.id_;
            fake_rsp_->reqid_ = req2->reqid_;
            fake_rsp_->type_ = kMsgType_PREPARE_RSP;
            rsp = std::move(fake_rsp_);
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
        std::shared_ptr<PaxosMsg> accepted_;
        std::shared_ptr<PaxosMsg> promised_;
        std::shared_ptr<PaxosMsg> fake_rsp_;
};

struct DummyEntryMng: public EntryMng {
    DummyEntryMng(std::shared_ptr<Configure> config, uint64_t pinst)
        :EntryMng(std::move(config), pinst) {}

    virtual int SaveEntry(uint64_t pinst, uint64_t entry, const Entry& ent) {
        return kErrCode_OK;
    }
    virtual int LoadEntry(uint64_t pinst, uint64_t entry, Entry& ent) {
        return kErrCode_OK;
    }
    virtual int Checkpoint(uint64_t pinst, uint64_t term) {
        return kErrCode_OK;
    }

    virtual uint64_t GetMaxCommittedEntry(uint64_t pinst) {
        return max_committed_;
    }

    virtual int LoadUncommittedEntry(std::vector<std::unique_ptr<Entry>>& entries) {
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

    auto entry_mng_creator = [](int pinst, std::shared_ptr<Configure> config) -> std::unique_ptr<EntryMng> {
        return std::unique_ptr<EntryMng>(new DummyEntryMng(std::move(config), pinst));
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

    // TODO
}
