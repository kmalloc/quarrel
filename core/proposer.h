#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "conn.h"
#include "ptype.h"
#include "config.h"

#include "idgen.hpp"
#include "waitgroup.hpp"
#include "timing_wheel.hpp"

#include <mutex>
#include <vector>
#include <memory>

namespace quarrel {

struct BatchRpcContext;

class Proposer {
 public:
  explicit Proposer(std::shared_ptr<Configure> config);
  ~Proposer() {}

  void SetConnMng(std::shared_ptr<ConnMng> mng) { conn_ = std::move(mng); }

  // propose a value
  int Propose(uint64_t opaque, const std::string& val, uint64_t paxos_inst = 0);

  // TODO, one prepare for mulitple consecutive entry slots
  int ProposeBatch(uint64_t opaque, const std::vector<std::string>& vals, uint64_t pinst);

  // update paxos instance states for chosen entry:
  // 1. chosen entry recovered from plog, just for the last chosen entry.
  // 2. entry written by remote peers, this is used to optimize first-write from slave.
  int OnEntryChosen(std::shared_ptr<PaxosMsg> msg, bool from_plog = false);

 private:
  struct InstanceState {
    // in-memory state of each paxos instance.
    // the very first proposal for each instance at startup requires a second try.
    // since the first try will always fail.
    // same logic applys for proposing from slave for the moment
    // a potential optimization for this particular case(write from slave) can be achieved by receiving chosen msg from acceptor.
    uint32_t proposer_id_;  // proposer id for current proposer, this is varied for each instance.
    uint32_t last_chosen_from_;
    uint64_t last_chosen_entry_;

    // idgen will be reset when the latest entry is chosen
    IdGen ig_;         // proposal id generator
    IdGenByDate vig_;  // value id generator

    /*
    // following used for concurrent control on the same instance.

    // instance state: 0 not used 1 waiting for timeout, 2 timeouted 3 canceled
    std::atomic<uint32_t> state_;

    // ongoing req for acceptors
    std::shared_ptr<PaxosMsg> req_;

    // rsp from acceptors for ongoing ballot
    std::vector<std::shared_ptr<PaxosMsg>> rsp_;

    using RpcRspHandler = std::function<int(std::shared_ptr<PaxosMsg>&)>;
    */

    InstanceState(int pid, int proposer_count)
        : proposer_id_(pid), last_chosen_from_(~0u), last_chosen_entry_(0), ig_(pid + 1, proposer_count), vig_(0xff, 1) {}
  };

 private:
  int doAccept(std::shared_ptr<PaxosMsg>& p);
  int doPrepare(std::shared_ptr<PaxosMsg>& p);
  int doChosen(std::shared_ptr<PaxosMsg>& p);

  bool canSkipPrepare(const Proposal&);
  std::shared_ptr<PaxosMsg> allocPaxosMsg(uint64_t pinst, uint64_t opaque, uint32_t value_size);

  bool UpdatePrepareId(uint64_t pinst, uint64_t pid);
  bool UpdateChosenInfo(uint64_t pinst, uint64_t chosen, uint64_t from);
  bool UpdateLocalStateFromRemoteMsg(std::shared_ptr<PaxosMsg>&);

  std::shared_ptr<BatchRpcContext> doBatchRpcRequest(int majority, std::shared_ptr<PaxosMsg>& pm);

 private:
  std::shared_ptr<ConnMng> conn_;
  std::shared_ptr<Configure> config_;

  std::vector<std::mutex> locks_;
  std::vector<InstanceState> states_;

  // TimingWheel timeout_;
};

}  // namespace quarrel

#endif
