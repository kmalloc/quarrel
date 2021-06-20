#ifndef __QUARREL_PAXOS_H_
#define __QUARREL_PAXOS_H_

#include <future>
#include <string>
#include <vector>
#include <memory>

#include "conn.h"
#include "plog.h"
#include "ptype.h"
#include "proposer.h"
#include "acceptor.h"
#include "config.h"

namespace quarrel {

class Paxos {
 public:
  explicit Paxos(std::unique_ptr<Configure> config);

  ~Paxos();

  int Start();
  int Stop();

  // submit local chosen-proposal to db
  int SubmitChosenProposal(int inst);

  // Propose() try to propose a new value.
  // empty value indicates a read probe, testing whether local is up to date.
  // paxos_inst: the paxos instance to write to, default is 0 for single instance paxos.
  // total number of instances is expected to be relativelly small(maybe < 1000).
  // for a cluster of servers, however, instance count could be really large.
  // users need to handle these instances mapping from global to local.
  // from a synod's point of view, it processes only a small consecutive array of instances.
  // but globally, every synod handles unique set of consecutive paxos instances.
  int Propose(uint64_t opaque, const std::string& value, uint64_t paxos_inst = 0);

  std::future<int> ProposeAsync(uint64_t opaque, const std::string& value, uint64_t paxos_inst = 0);

 private:
  Paxos(const Paxos&) = delete;
  Paxos& operator=(const Paxos&) = delete;

  std::thread thread_;
  bool started_{false};

  // these most basic info should come first.
  std::shared_ptr<Configure> config_;
  std::shared_ptr<ConnMng> conn_mng_;
  std::shared_ptr<PlogMng> plog_mng_;
  std::shared_ptr<PaxosGroupBase> pg_mapper_;

  // those use basic info comes after.
  Acceptor acceptor_;
  Proposer proposer_;
};

}  // namespace quarrel

#endif
