#include "paxos.h"

namespace quarrel {

    Paxos::Paxos(std::unique_ptr<Configure> config)
        : config_(std::move(config))
        , conn_mng_(config_)
        , acceptor_(config_), proposer_(config_) {}

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
}
