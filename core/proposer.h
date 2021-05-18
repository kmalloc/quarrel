#ifndef __QUARREL_PROPOSER_H_
#define __QUARREL_PROPOSER_H_

#include "conn.h"
#include "plog.h"
#include "config.h"
#include "ptype.h"
#include "idgen.hpp"
#include "waitgroup.hpp"

#include <vector>
#include <memory>

namespace quarrel {

struct BatchRpcContext {
  int ret_;
  WaitGroup wg_;
  std::atomic<int> rsp_count_;
  std::shared_ptr<PaxosMsg> rsp_msg_[MAX_ACCEPTOR_NUM];

  BatchRpcContext(int expect_rsp_count)
      : wg_(expect_rsp_count), rsp_count_(0) {}
};

class Proposer {
 public:
  explicit Proposer(std::shared_ptr<Configure> config);
  ~Proposer() {}

  void SetPlogMng(std::shared_ptr<PlogMng> mng) { pmn_ = std::move(mng); }
  void SetConnMng(std::shared_ptr<ConnMng> mng) { conn_ = std::move(mng); }

  // propose a value
  // current implementation permits concurrent Propose() from different threads.
  // this is no harm at the cost of some performance overhead, which can be
  // avoided by putting requests in a queue, or acquire an instance-level lock for each propose.
  int Propose(uint64_t opaque, const std::string& val, uint64_t paxos_inst = 0);

  // TODO, one prepare for mulitple consecutive entry slots
  int ProposeBatch(uint64_t opaque, const std::vector<std::string>& vals, uint64_t pinst);

 private:
  int doAccept(std::shared_ptr<PaxosMsg>& p);
  int doPrepare(std::shared_ptr<PaxosMsg>& p);
  int doChosen(std::shared_ptr<PaxosMsg>& p);
  bool canSkipPrepare(uint64_t pinst, uint64_t entry);
  std::shared_ptr<PaxosMsg> allocPaxosMsg(uint64_t pinst, uint64_t opaque,
                                          uint32_t value_size);
  std::shared_ptr<BatchRpcContext> doBatchRpcRequest(
      int majority, std::shared_ptr<PaxosMsg>& pm);

 private:
  std::shared_ptr<PlogMng> pmn_;
  std::shared_ptr<ConnMng> conn_;
  std::shared_ptr<Configure> config_;
};

}  // namespace quarrel

#endif