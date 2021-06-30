#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "plog.h"
#include "conn.h"
#include "config.h"
#include "worker_pool.hpp"

#include <memory>
#include <future>
#include <vector>
#include <functional>

namespace quarrel {

class Acceptor {
 public:
  explicit Acceptor(std::shared_ptr<Configure> config);
  ~Acceptor();

  // An acceptor maintains several worker threads,
  // each thread waits on a msg queue designated to several plog instances,
  int StartWorker();
  int StopWorker();

  // note: every request required a response.
  int AddMsg(std::shared_ptr<PaxosMsg> m, ResponseCallback cb);
  std::future<std::shared_ptr<PaxosMsg>> AddMsgAsync(std::shared_ptr<PaxosMsg> m);

  // broadcast chosen msg
  // used to optimize proposer states
  using ChosenNotifyFunc = std::function<int(std::shared_ptr<PaxosMsg>)>;

  void AddChosenNotify(ChosenNotifyFunc);

  void SetPlogMng(std::shared_ptr<PlogMng> pm) { pmn_ = std::move(pm); }

  void SetConfig(std::shared_ptr<Configure> config) {
    config_ = std::move(config);
  }

 private:
  struct PaxosRequest {
    ResponseCallback cb_;
    std::shared_ptr<PaxosMsg> msg_;
  };

  Acceptor(const Acceptor&) = delete;
  Acceptor& operator=(const Acceptor&) = delete;

  std::shared_ptr<PaxosMsg> handleAcceptReq(Proposal& proposal);
  std::shared_ptr<PaxosMsg> handlePrepareReq(Proposal& proposal);
  std::shared_ptr<PaxosMsg> handleChosenReq(Proposal& proposal);

  void doCatchupFromPeer();
  void TriggerLocalCatchup();
  int doHandleMsg(PaxosRequest req);
  int CheckLocalAndMayTriggerCatchup(const Proposal& pp);

 private:
  uint64_t term_;  // logical time
  WorkerPool<PaxosRequest> wpool_;

  std::shared_ptr<PlogMng> pmn_;
  std::shared_ptr<Configure> config_;
  std::vector<ChosenNotifyFunc> notification_;
};

}  // namespace quarrel

#endif
