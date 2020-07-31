#include "paxos.h"

namespace quarrel {

    Paxos::Paxos(std::unique_ptr<Configure> config)
        : config_(std::move(config))
        , conn_mng_(config_)
        , acceptor_(config_), proposer_(config_) {}
}
