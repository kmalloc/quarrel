#include "acceptor.h"

#include <assert.h>

namespace quarrel {

Acceptor::Acceptor(std::shared_ptr<Configure> config)
    : config_(std::move(config)) {}

Acceptor::~Acceptor() { StopWorker(); }

int Acceptor::StartWorker() {
  if (started_) return kErrCode_WORKER_ALREADY_STARTED;

  auto num = config_->acceptor_worker_count_;
  assert(config_->acceptor_worker_count_ < config_->plog_inst_num_);

  run_ = 1;
  workers_.clear();
  workers_.reserve(num);

  for (auto i = 0u; i < num; i++) {
    auto wd = std::unique_ptr<WorkerData>(new WorkerData);
    wd->wg_.Reset(1);
    wd->mq_.Init(config_->worker_msg_queue_sz_);
    wd->th_ = std::thread(&Acceptor::WorkerProc, this, i);
    workers_.push_back(std::move(wd));
  }

  started_ = true;
  return kErrCode_OK;
}

int Acceptor::StopWorker() {
  run_ = 0;
  for (auto i = 0u; i < workers_.size(); i++) {
    workers_[i]->th_.join();
  }
  started_ = false;
  return kErrCode_OK;
}

int Acceptor::AddMsg(std::shared_ptr<PaxosMsg> msg, ResponseCallback cb) {
  if (!started_) return kErrCode_WORKER_NOT_STARTED;

  auto pp = GetProposalFromMsg(msg.get());
  auto pinst = pp->plid_;
  auto idx = pinst % workers_.size();

  PaxosRequest req;
  req.cb_ = std::move(cb);
  req.msg_ = std::move(msg);

  workers_[idx]->mq_.Enqueue(std::move(req), false);
  if (workers_[idx]->pending_.fetch_add(1) == 0) {
    workers_[idx]->wg_.Notify();
  }

  return kErrCode_OK;
}

int Acceptor::WorkerProc(int workerid) {
  using QueueType = LockFreeQueue<PaxosRequest>;
  std::unique_ptr<WorkerData>& queue = workers_[workerid];

  while (run_ > 0) {
    PaxosRequest req;
    if (queue->mq_.Dequeue(req, false) == QueueType::RT_EMPTY) {
      queue->wg_.Wait(100);
      continue;
    }

    DoHandleMsg(std::move(req));
    queue->pending_.fetch_sub(1);
  }

  return 0;
}

int Acceptor::DoHandleMsg(PaxosRequest req) {
  auto pp = GetProposalFromMsg(req.msg_.get());
  auto mtype = req.msg_->type_;

  std::shared_ptr<PaxosMsg> rsp;

  if (mtype == kMsgType_PREPARE_REQ) {
    rsp = HandlePrepareReq(*pp);
  } else if (mtype == kMsgType_ACCEPT_REQ) {
    rsp = HandleAcceptReq(*pp);
  } else if (mtype == kMsgType_CHOSEN_REQ) {
    rsp = HandleChosenReq(*pp);
  } else {
    rsp = std::make_shared<PaxosMsg>();
    memcpy(rsp.get(), req.msg_.get(), PaxosMsgHeaderSz);

    rsp->size_ = 0;
    rsp->from_ = config_->local_id_;
    rsp->type_ = kMsgType_INVALID_REQ;
  }

  req.cb_(rsp);
  return kErrCode_OK;
}

std::shared_ptr<PaxosMsg> Acceptor::HandlePrepareReq(const Proposal& pp) {
  auto pinst = pp.plid_;
  auto entry = pp.pentry_;
  auto status = pp.status_;
  // TODO
  (void)pinst;
  (void)entry;
  (void)status;
  return NULL;
}

}  // namespace quarrel
