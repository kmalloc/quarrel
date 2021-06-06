#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "queue.h"
#include "plog.h"
#include "conn.h"
#include "config.h"
#include "waitgroup.hpp"

#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <functional>

namespace quarrel {

struct PaxosRequest {
  ResponseCallback cb_;
  std::shared_ptr<PaxosMsg> msg_;
};

// will be indexed by thread num, make sure it is 64 byte aligned, so that no
// false sharing will occur.
struct WorkerData {
  WaitGroup wg_;
  std::thread th_;
  std::atomic<uint64_t> pending_;
  LockFreeQueue<PaxosRequest> mq_;
};

class Acceptor {
 public:
  explicit Acceptor(std::shared_ptr<Configure> config);
  ~Acceptor();

  // An acceptor maintains several worker threads,
  // each thread waits on a msg queue designated to several plog instances,
  // thread count must <= plog instance count,
  // ensuring that each plog instance is mutated from one thread only.
  int StartWorker();
  int StopWorker();

  // note: every request required a response.
  int AddMsg(std::shared_ptr<PaxosMsg> m, ResponseCallback cb);

  // broadcast chosen msg
  // used to optimize proposer states
  using ChosenNotifyFunc = std::function<int(std::shared_ptr<PaxosMsg>)>;

  void AddChosenNotify(ChosenNotifyFunc);

  void SetPlogMng(std::shared_ptr<PlogMng> pm) { pmn_ = std::move(pm); }

  void SetConfig(std::shared_ptr<Configure> config) {
    config_ = std::move(config);
  }

 private:
  Acceptor(const Acceptor&) = delete;
  Acceptor& operator=(const Acceptor&) = delete;

  std::shared_ptr<PaxosMsg> handleAcceptReq(Proposal& proposal);
  std::shared_ptr<PaxosMsg> handlePrepareReq(Proposal& proposal);
  std::shared_ptr<PaxosMsg> handleChosenReq(Proposal& proposal);

  int workerProc(int workerid);
  int doHandleMsg(PaxosRequest req);
  void doCatchupFromPeer(Proposal& pp);
  int CheckLocalAndMayTriggerCatchup(const Proposal& pp);

 private:
  uint64_t term_;  // logical time
  bool started_{false};
  std::atomic<uint8_t> run_{0};

  std::shared_ptr<PlogMng> pmn_;
  std::shared_ptr<Configure> config_;
  std::vector<WorkerData> workers_;
  std::vector<ChosenNotifyFunc> notification_;
};

}  // namespace quarrel

#endif
