#include "paxos.h"

namespace quarrel {
Paxos::Paxos(std::unique_ptr<Configure> config)
    : config_(std::move(config)),
      acceptor_(config_),
      proposer_(config_) {
  if (config->pg_type_ == PGT_Quorum3) {
    pg_mapper_.reset(new PaxosGroup3);
  } else if (config->pg_type_ == PGT_Quorum5) {
    pg_mapper_.reset(new PaxosGroup5);
  } else {
    assert(0);
  }

  plog_mng_ = std::make_shared<PlogMng>(config_, pg_mapper_);
  conn_mng_ = std::make_shared<ConnMng>(config_, pg_mapper_);

  proposer_.SetConnMng(conn_mng_);
  acceptor_.SetPlogMng(plog_mng_);
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
}  // namespace quarrel
