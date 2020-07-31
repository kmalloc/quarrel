#include "paxos.h"

namespace quarrel {
    Paxos::Paxos(std::unique_ptr<Configure> config)
        : config_(std::move(config))
        , conn_mng_(std::make_shared<ConnMng>(config_))
        , plog_mng_(std::make_shared<PlogMng>(config_))
        , acceptor_(config_), proposer_(config_) {
            proposer_.SetConnMng(conn_mng_);
            proposer_.SetPlogMng(plog_mng_);
    }

    Paxos::~Paxos() {}

    int Paxos::Start() {
        if (started_) return 0;

        started_ = true;
        return acceptor_.StartWorker();
    }

    int Paxos::Stop() {
        if (!started_) return 0;

        return acceptor_.StopWorker();
    }

    int Paxos::Propose(uint64_t opaque, const std::string& value, uint64_t pinst) {
        return proposer_.Propose(opaque, value, pinst);
    }
}
