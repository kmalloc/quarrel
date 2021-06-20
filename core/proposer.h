#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "conn.h"
#include "ptype.h"
#include "config.h"

#include "worker_pool.hpp"
#include "timing_wheel.hpp"

#include <mutex>
#include <tuple>
#include <vector>
#include <memory>
#include <future>

namespace quarrel {

struct InstanceState;

class Proposer {
 public:
  explicit Proposer(std::shared_ptr<Configure> config);
  ~Proposer();

  void SetConnMng(std::shared_ptr<ConnMng> mng) { conn_ = std::move(mng); }

  // propose a value
  int Propose(uint64_t opaque, const std::string& val, uint64_t paxos_inst = 0);

  std::future<int> ProposeAsync(uint64_t opaque, const std::string& val, uint64_t paxos_inst = 0);

  // TODO, one prepare for mulitple consecutive entry slots
  int ProposeBatch(uint64_t opaque, const std::vector<std::string>& vals, uint64_t pinst);

  // update paxos instance states for chosen entry:
  // 1. chosen entry recovered from plog, just for the last chosen entry.
  // 2. entry written by remote peers, this is used to optimize first-write from slave.
  int OnEntryChosen(std::shared_ptr<PaxosMsg> msg, bool from_plog = false);

 private:
  int doAccept(std::shared_ptr<PaxosMsg>& p);
  int doChosen(std::shared_ptr<PaxosMsg>& p);
  int doPrepare(std::shared_ptr<PaxosMsg>& p);

  int handleAcceptRsp(std::shared_ptr<PaxosMsg> rsp);
  int handlePrepareRsp(std::shared_ptr<PaxosMsg> rsp);

  bool canSkipPrepare(const Proposal&);
  std::shared_ptr<PaxosMsg> allocPaxosMsg(uint64_t pinst, uint64_t opaque, uint32_t value_size);

  bool UpdatePrepareId(uint64_t pinst, uint64_t pid);
  bool UpdateChosenInfo(uint64_t pinst, uint64_t chosen, uint64_t from);

  int doBatchRpcRequest(std::shared_ptr<PaxosMsg>& pm);
  int addRpcResponse(uint64_t pinst, uint64_t entry, std::shared_ptr<PaxosMsg> m, uint64_t term);

  bool addTimeout(uint64_t pinst, uint64_t entry, uint64_t term);
  void setupWaitState(uint64_t pinst, uint64_t entry, uint32_t state, std::promise<int>* promise, std::shared_ptr<PaxosMsg>* pm);

  struct RpcResponseMsg {
    uint64_t term_{0};
    uint64_t pinst_{0};
    uint64_t entry_{0};
    std::shared_ptr<PaxosMsg> msg_;
  };

  int handleRpcResponse(RpcResponseMsg m);

 private:
  std::shared_ptr<ConnMng> conn_;
  std::shared_ptr<Configure> config_;

  std::vector<std::mutex> locks_;
  std::unique_ptr<InstanceState[]> states_;

  TimingWheel timeout_;
  WorkerPool<RpcResponseMsg> wpool_;
};

}  // namespace quarrel

#endif
