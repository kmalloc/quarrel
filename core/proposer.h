#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "conn.h"
#include "plog.h"
#include "ptype.h"
#include "config.h"

#include "idgen.hpp"
#include "waitgroup.hpp"

#include <mutex>
#include <vector>
#include <memory>
#include <unordered_map>

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

 private:
  // in-memory state of each paxos instance.
  // the very first proposal for each instance at startup requires a second try.
  // since the first try will always fail.
  // same logic applys for proposing from slave for the moment
  // a potential optimization for this particular case(write from slave) can be achieved by receiving chosen msg from acceptor.
  struct InstanceState {
    uint64_t last_chosen_from_;
    uint64_t last_chosen_entry_;

    // idgen will be reset when the latest entry is chosen
    IdGen ig_;         // proposal id generator
    IdGenByDate vig_;  // value id generator

    InstanceState(int svrid, int proposer_count)
        : last_chosen_from_(~0u), last_chosen_entry_(0), ig_(svrid, proposer_count), vig_(0xff, 1) {}
  };

 private:
  int doAccept(std::shared_ptr<PaxosMsg>& p);
  int doPrepare(std::shared_ptr<PaxosMsg>& p);
  int doChosen(std::shared_ptr<PaxosMsg>& p);

  // update paxos instance states for chosen entry by remote peers.
  int onChosenNotify(std::shared_ptr<PaxosMsg> msg);

  bool canSkipPrepare(uint64_t pinst, uint64_t entry);
  std::shared_ptr<PaxosMsg> allocPaxosMsg(uint64_t pinst, uint64_t opaque,
                                          uint32_t value_size);
  std::shared_ptr<BatchRpcContext> doBatchRpcRequest(
      int majority, std::shared_ptr<PaxosMsg>& pm);

  bool UpdatePrepareId(uint64_t pinst, uint64_t pid);
  bool UpdateChosenInfo(uint64_t pinst, uint64_t chosen, uint64_t from);

 private:
  std::shared_ptr<ConnMng> conn_;
  std::shared_ptr<Configure> config_;

  std::vector<std::mutex> locks_;
  std::vector<InstanceState> states_;
};

}  // namespace quarrel

#endif
