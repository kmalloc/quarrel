#include "paxos.h"

namespace quarrel {
Paxos::Paxos(std::unique_ptr<Configure> config)
    : config_(std::move(config)),
      acceptor_(config_),
      proposer_(config_) {
  auto mp = PaxosGroupBase::CreateGroup(config_->pg_type_);

  pg_mapper_.reset(mp.release());
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

std::future<int> Paxos::ProposeAsync(uint64_t opaque, const std::string& value, uint64_t paxos_inst) {
  return proposer_.ProposeAsync(opaque, value, paxos_inst);
}

}  // namespace quarrel
