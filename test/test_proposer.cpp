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
            data.cb_(std::move(rsp));
            return 0;
        }
};

struct DummyRemoteConn: public RemoteConn {
    public:
        DummyRemoteConn(AddrInfo addr): RemoteConn(100, std::move(addr)) {}
        virtual ~DummyRemoteConn() {}

        virtual int DoWrite(std::shared_ptr<PaxosMsg> req) {
          auto fake_rsp = [self = this](std::shared_ptr<PaxosMsg> msg) mutable {
            auto tm = std::chrono::milliseconds(1);
            std::this_thread::sleep_for(tm);
            self->HandleRecv(msg);
            LOG_INFO << "dummy call to HandleRecv()";
          };

          std::async(std::launch::async, fake_rsp, std::move(req));

          LOG_INFO << "dummy call to DoWrite()";
          return kErrCode_OK;
        }
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
    config->local_ = {ConnType_LOCAL, "xxxx:yyy"};
    config->total_acceptor_ = 3;
    config->plog_inst_num_ = 5;
    config->peer_.push_back({ConnType_REMOTE, "aaaa:bb"});
    config->peer_.push_back({ConnType_REMOTE, "aaaa2:bb2"});

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

    ASSERT_EQ(kErrCode_OK,pp.Propose(0xbadf00d, "dummy value"));

    // TODO
}
