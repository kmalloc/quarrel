#include "acceptor.h"

#include "logger.h"
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
    workers_[i]->wg_.Notify();
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
    rsp->type_ = kMsgType_PREPARE_RSP;
  } else if (mtype == kMsgType_ACCEPT_REQ) {
    rsp = HandleAcceptReq(*pp);
    rsp->type_ = kMsgType_ACCEPT_RSP;
  } else if (mtype == kMsgType_CHOSEN_REQ) {
    rsp = HandleChosenReq(*pp);
    rsp->type_ = kMsgType_CHOSEN_RSP;
  } else {
    rsp = std::make_shared<PaxosMsg>();
    memcpy(rsp.get(), req.msg_.get(), PaxosMsgHeaderSz);

    rsp->size_ = 0;
    rsp->type_ = kMsgType_INVALID_REQ;
  }

  if (!rsp) {
      // chosen req doesn't require a response.
      return kErrCode_OK;
  }

  rsp->from_ = config_->local_id_;
  rsp->version_ = req.msg_->version_;
  rsp->magic_ = req.msg_->magic_;
  rsp->reqid_ =req.msg_->reqid_;
  req.cb_(rsp);
  return kErrCode_OK;
}

std::shared_ptr<PaxosMsg> Acceptor::HandlePrepareReq(const Proposal& pp) {
  auto pinst = pp.plid_;
  auto entry = pp.pentry_;
  std::shared_ptr<PaxosMsg> rsp;

  auto& ent = pmn_->GetEntry(pinst, entry);

  const auto& existed_pp = ent.GetProposal();
  const auto& existed_promise = ent.GetPromised();

  auto vsize = 0;
  const Proposal* from_pp = NULL;
  int status = kPaxosState_PROMISED;

  if (existed_pp) {
      // largest last vote
      vsize = existed_pp->size_;
      from_pp = existed_pp.get();
      status = kPaxosState_ACCEPTED;
  } else if (existed_promise && existed_promise->pid_ >= pp.pid_) {
      // reject for previous promise
      vsize = existed_promise->size_;
      from_pp = existed_promise.get();
  } else {
      // a new proposal request
      from_pp = &pp;
      vsize = pp.size_;
      auto ret = pmn_->SetPromised(pp);
      if (ret != kErrCode_OK) {
        vsize = 0;
        status = kPaxosState_PROMISED_FAILED;
      }
  }

  auto ret = AllocProposalMsg(vsize);
  auto rpp = GetProposalFromMsg(ret.get());

  ret->type_ = kMsgType_PREPARE_RSP;
  memcpy(rpp, from_pp, ProposalHeaderSz + vsize);
  rpp->status_ = status;

  return std::move(ret);
}

std::shared_ptr<PaxosMsg> Acceptor::HandleAcceptReq(const Proposal& pp) {
  auto pinst = pp.plid_;
  auto entry = pp.pentry_;
  std::shared_ptr<PaxosMsg> rsp;

  auto& ent = pmn_->GetEntry(pinst, entry);

  bool accepted = false;
  auto accepted_pp = &pp;

  const auto& existed_pp = ent.GetProposal();
  const auto& existed_promise = ent.GetPromised();

  if (existed_pp) {
    // case 1:
    // A prepare p1 to C
    // C reponse with a promise.
    // B prepare p2 to C
    // C response with a promise
    // B send accept to C
    // A send accept to C
    // case 2: proposer proposes to an entry which already accepted a proposal
    auto status = existed_pp->status_;
    if (status != kPaxosState_CHOSEN && status != kPaxosState_ACCEPTED) {
      LOG_ERR << "invalid status of accepted proposal found, (pinst, entry):("
              << pinst << "," << entry << "), status:" << status;
    }

    accepted_pp = existed_pp.get();
    if (existed_pp->pid_ == pp.pid_ || existed_pp->value_id_ == pp.value_id_) {
      accepted = true;
    }
  } else if (existed_promise) {
    if (existed_promise->pid_ <= pp.pid_) {
      // in case of #0 proposal optimization.
      // existed promise id is very likely to less than accepted id.
      // in which case non-master proposer proposes before master.
      if (pmn_->SetAccepted(pp) == kErrCode_OK) {
        accepted = true;
      }
    }
  }

  auto ret = AllocProposalMsg(0);
  auto rpp = GetProposalFromMsg(ret.get());

  memcpy(rpp, accepted_pp, ProposalHeaderSz);

  rpp->size_ = 0;
  ret->type_ = kMsgType_ACCEPT_RSP;

  if (accepted) {
      rpp->status_ = kPaxosState_ACCEPTED;
  } else {
      rpp->status_ = kPaxosState_ACCEPTED_FAILED;
  }

  // clear promised
  pmn_->ClearPromised(pinst, entry);

  return ret;
}

std::shared_ptr<PaxosMsg> Acceptor::HandleChosenReq(const Proposal& pp) {
  auto pinst = pp.plid_;
  auto entry = pp.pentry_;

  auto& ent = pmn_->GetEntry(pinst, entry);
  const auto& existed_pp = ent.GetProposal();

  if (!existed_pp || pp.pid_ != existed_pp->pid_ || pp.value_id_ != existed_pp->value_id_) {
    LOG_ERR << "invalid chosen request, proposal has change, pinst:" << pinst
            << ", entry:" << entry << ", req pid:" << pp.pid_
            << ", local pid:" << (existed_pp ? existed_pp->pid_ : ~0)
            << ", req vid:" << pp.value_id_
            << ", local vid:" << (existed_pp ? existed_pp->value_id_ : ~0);
    return NULL;
  }

  auto err = pmn_->CommitEntry(pinst, entry);
  if (err != kErrCode_OK) {
    LOG_ERR << "commit entry failed, pinst:" << pinst << ", entry:" << entry
            << ", err:" << err << ", pid:" << pp.pid_
            << ", vid:" << pp.value_id_;
  }

  return NULL;
}

}  // namespace quarrel
