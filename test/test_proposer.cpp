#include "gtest/gtest.h"

#include <string>
#include <thread>

#include "proposer.h"

using namespace quarrel;

struct DummyLocalConn: public LocalConn {
    public:
        DummyLocalConn(AddrInfo addr): LocalConn(std::move(addr)) {}

        virtual int DoRpcRequest(RpcReqData data) {
            auto rsp = CloneProposalMsg(*data.data_.get());
            data.cb_(rsp);
            return 0;
        }
};

struct DummyRemoteConn: public RemoteConn {
    public:
        DummyRemoteConn(AddrInfo addr): RemoteConn(100, std::move(addr)) {}
        virtual ~DummyRemoteConn() {}

        virtual int DoWrite(std::shared_ptr<PaxosMsg> req) {
            return 0;
        }
};

TEST(proposer, doPropose) {
    auto config = std::make_shared<Configure>();
    config->timeout_ = 3; // 3ms
    config->local_ = {ConnType_LOCAL, "xxxx:yyy"};
    config->total_acceptor_ = 3;
    config->peer_.push_back({ConnType_Remote, "aaaa:bb"});
    config->peer_.push_back({ConnType_Remote, "aaaa2:bb2"});

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

    ASSERT_EQ(ConnType_Remote, r1->GetType());
    ASSERT_STREQ("aaaa:bb", r1->GetAddr().addr_.c_str());
    ASSERT_EQ(ConnType_Remote, r2->GetType());
    ASSERT_STREQ("aaaa2:bb2", r2->GetAddr().addr_.c_str());

    //PlogMng pmn;
    //Proposer pp(config);
    //pp.SetConnMng(conn_mng);

    // TODO
}
