#include "gtest/gtest.h"

#include <string>
#include <thread>

#include "proposer.h"

using namespace quarrel;

struct DummyConn: public Conn {
    DummyConn(AddrInfo addr): Conn(std::move(addr)) {}
    virtual int DoRpcRequest(RpcReqData data) {
        data.cb_(data.data_);
        return 0;
    }
    virtual ~DummyConn() {}
};

struct DummyRemoteConn: public DummyConn {
    public:
        DummyRemoteConn(AddrInfo addr): DummyConn(std::move(addr)) {}
        virtual int HandleRecv(std::unique_ptr<PaxosMsg> req) {
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
        if (addr.type_ == ConnType_LOCAL) return std::unique_ptr<DummyConn>(new DummyConn(std::move(addr)));

        return std::unique_ptr<DummyRemoteConn>(new DummyRemoteConn(std::move(addr)));
    };

    ConnMng conn_mng(config);
    conn_mng.SetConnCreator(conn_creator);
}
