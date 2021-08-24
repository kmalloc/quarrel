#include "acceptor.h"

#include "logger.h"
#include <assert.h>

namespace quarrel {

Acceptor::Acceptor(std::shared_ptr<Configure> config)
    : wpool_([this](PaxosRequest d) { return doHandleMsg(std::move(d)); }), config_(std::move(config)) {}

Acceptor::~Acceptor() { StopWorker(); }

int Acceptor::StartWorker() {
  assert(pmn_);

  auto num = config_->acceptor_worker_count_;
  assert(config_->acceptor_worker_count_ < config_->plog_inst_num_);

  wpool_.StartWorker(num, config_->worker_msg_queue_sz_);
  return kErrCode_OK;
}

int Acceptor::StopWorker() {
  wpool_.StopWorker();
  return kErrCode_OK;
}

std::future<std::shared_ptr<PaxosMsg>> Acceptor::AddMsgAsync(std::shared_ptr<PaxosMsg> msg) {
  auto pms = std::make_shared<std::promise<std::shared_ptr<PaxosMsg>>>();
  auto cb = [=](std::shared_ptr<PaxosMsg> m) mutable { pms->set_value(std::move(m));return 0; };

  auto ret = AddMsg(std::move(msg), std::move(cb));
  if (ret) {
    auto m = AllocProposalMsg(0);
    m->errcode_ = ret;
    cb(std::move(m));
  }

  return pms->get_future();
}

int Acceptor::AddMsg(std::shared_ptr<PaxosMsg> msg, ResponseCallback cb) {
  auto pp = GetProposalFromMsg(msg.get());
  auto pinst = pp->pinst_;

  PaxosRequest req;
  req.cb_ = std::move(cb);
  req.msg_ = std::move(msg);

  auto ret = wpool_.AddWork(pinst, std::move(req));
  if (ret) {
    return kErrCode_ACCEPTOR_QUEUE_FULL;
  }

  return kErrCode_OK;
}

int Acceptor::doHandleMsg(PaxosRequest req) {
  auto pp = GetProposalFromMsg(req.msg_.get());
  auto mtype = req.msg_->type_;

  std::shared_ptr<PaxosMsg> rsp;

  if (mtype == kMsgType_PREPARE_REQ) {
    rsp = handlePrepareReq(*pp);
    rsp->type_ = kMsgType_PREPARE_RSP;
  } else if (mtype == kMsgType_ACCEPT_REQ) {
    rsp = handleAcceptReq(*pp);
    rsp->type_ = kMsgType_ACCEPT_RSP;
  } else if (mtype == kMsgType_CHOSEN_REQ) {
    rsp = handleChosenReq(*pp);
    rsp->type_ = kMsgType_CHOSEN_RSP;
  } else if (mtype == kMsgType_CHORE_CATCHUP) {
    doCatchupFromPeer(*pp);
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

  rsp->from_ = config_->local_.id_;
  rsp->version_ = req.msg_->version_;
  rsp->magic_ = req.msg_->magic_;
  rsp->reqid_ =req.msg_->reqid_;
  req.cb_(rsp);
  return kErrCode_OK;
}

void Acceptor::doCatchupFromPeer(Proposal& pp) {
  auto pinst = pp.pinst_;
  auto target = pp.pentry_;
  auto local_chosen = pmn_->GetMaxChosenEntry(pinst);
  auto commit_entry = pmn_->GetMaxCommittedEntry(pinst);
  auto global_chosen = pmn_->GetGlobalMaxChosenEntry(pinst);

  (void)pinst;
  (void)target;
  (void)local_chosen;
  (void)commit_entry;
  (void)global_chosen;

  // do local commit for entry commit_entry ~ local_chosen
  // FIXME

  // do catchup for entry from local_chosen+1 ~ global_chosen
  // FIXME
}

void Acceptor::TriggerLocalCatchup(uint64_t pinst, uint64_t target_entry) {
  auto req = AllocProposalMsg(0);
  auto pp = GetProposalFromMsg(req.get());

  pp->pinst_ = pinst;
  pp->pentry_ = target_entry;
  req->type_ = kMsgType_CHORE_CATCHUP;
  AddMsg(std::move(req), [](std::shared_ptr<PaxosMsg>) { return 0; });
}

int Acceptor::CheckLocalAndMayTriggerCatchup(const Proposal& pp) {
  auto pinst = pp.pinst_;
  auto entry = pp.pentry_;
  auto remote_last_chosen = pp.last_chosen_;

  auto local_last_chosen = pmn_->GetMaxChosenEntry(pinst);
  if (local_last_chosen != ~0ull && local_last_chosen >= entry) {
    return kErrCode_REMOTE_NEED_CATCHUP;
  }

  // catchup contains 2 parts:
  // 1. commit globally accepted entry to db,
  // 2. learn about the empty entry which already globally accepted.
  // one important aspect to note is that: hole is allowed in the slave(no hole in master)
  if (remote_last_chosen > local_last_chosen) {
    pmn_->SetGlobalMaxChosenEntry(pinst, remote_last_chosen);
    TriggerLocalCatchup(pinst, local_last_chosen + 1);
    // return kErrCode_NEED_CATCHUP;
  }

  return kErrCode_OK;
}

std::shared_ptr<PaxosMsg> Acceptor::handlePrepareReq(Proposal& pp) {
  auto pinst = pp.pinst_;
  auto entry = pp.pentry_;
  std::shared_ptr<PaxosMsg> rsp;

  auto vsize = 0;
  uint32_t errcode = 0;
  const Proposal* from_pp = &pp;
  int status = kPaxosState_PROMISED;

  // trigger a catchup if current acceptor lags behind
  auto ret = CheckLocalAndMayTriggerCatchup(pp);
  if (ret) {
    errcode = ret;
    if (ret == kErrCode_NEED_CATCHUP) {
      status = kPaxosState_LOCAL_LAG_BEHIND;
    } else if (ret == kErrCode_REMOTE_NEED_CATCHUP) {
      status = kPaxosState_REMOTE_LAG_BEHIND;
    } else {
      assert(0);
    }
  } else {
    auto& ent = pmn_->GetEntryAndCreateIfNotExist(pinst, entry);
    const auto& existed_pp = ent.GetProposal();
    const auto& existed_promise = ent.GetPromised();

    if (existed_pp) {
      // largest last vote
      // if pid of accepted value > incoming pid
      // we can just reject it as well.
      // but here we don't do that since proposer will check response pid.
      // and reject client anyway.
      vsize = existed_pp->size_;
      from_pp = existed_pp.get();
      status = kPaxosState_ACCEPTED;
    } else if (existed_promise && existed_promise->pid_ >= pp.pid_) {
      // reject for previous promise
      vsize = existed_promise->size_;
      from_pp = existed_promise.get();
      // errcode = kErrCode_PREPARE_REJECTED;
    } else {
      // a new proposal request
      vsize = pp.size_;
      if (pp.pid_ > 0) {
        pp.status_ = kPaxosState_PROMISED;
        ret = pmn_->SetPromised(pp);
      } else {
        // pid == 0 indicates a read probe.
      }

      if (ret != kErrCode_OK) {
        vsize = 0;
        errcode = kErrCode_WRITE_PLOG_FAIL;
        status = kPaxosState_PROMISED_FAILED;
      }
    }
  }

  rsp = AllocProposalMsg(vsize);

  rsp->errcode_ = errcode;
  rsp->type_ = kMsgType_PREPARE_RSP;
  auto rpp = GetProposalFromMsg(rsp.get());

  memcpy(rpp, from_pp, ProposalHeaderSz + vsize);

  rpp->status_ = status;
  rpp->last_chosen_ = pmn_->GetMaxChosenEntry(pinst);
  rpp->last_chosen_from_ = uint16_t(~0u);

  return std::move(rsp);
}

std::shared_ptr<PaxosMsg> Acceptor::handleAcceptReq(Proposal& pp) {
  auto pinst = pp.pinst_;
  auto entry = pp.pentry_;
  std::shared_ptr<PaxosMsg> rsp;

  rsp = AllocProposalMsg(0);
  auto rpp = GetProposalFromMsg(rsp.get());
  rsp->type_ = kMsgType_ACCEPT_RSP;

  // trigger a catchup if current acceptor lags behind
  auto ret = CheckLocalAndMayTriggerCatchup(pp);
  if (ret) {
    rsp->errcode_ = ret;
    rpp->status_ = kPaxosState_LOCAL_LAG_BEHIND;
    rpp->last_chosen_from_ = uint16_t(~0u);
    rpp->last_chosen_ = pmn_->GetMaxChosenEntry(pinst);
    return std::move(rsp);
  }

  auto& ent = pmn_->GetEntryAndCreateIfNotExist(pinst, entry);

  bool accepted = false;
  auto accepted_pp = &pp;

  uint32_t errcode = 0;
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
    // case 2:
    // A proposer proposes to an entry which already accepted a proposal
    auto status = existed_pp->status_;
    if (status != kPaxosState_CHOSEN && status != kPaxosState_ACCEPTED) {
      errcode = kErrCode_INVALID_PLOG_DATA;
      LOG_ERR << "invalid status of accepted proposal found, (pinst, entry):("
              << pinst << "," << entry << "), status:" << status;
    } else if (existed_pp->pid_ == pp.pid_ && existed_pp->value_id_ == pp.value_id_) {
      // duplicate accept req
      accepted = true;
      accepted_pp = existed_pp.get();
      errcode = kErrCode_DUPLICATE_PROPOSAL_REQ;
    } else if (existed_pp->pid_ < pp.pid_) {
      if (status != kPaxosState_CHOSEN) {
        // renew accepted value
        accepted_pp = &pp;
        pp.status_ = kPaxosState_ACCEPTED;
        if (pmn_->SetAccepted(pp) == kErrCode_OK) {
          accepted = true;
          LOG_ERR << "renew accepted value succ, pinst:" << pinst
                  << ", entry:" << entry << ", pid:" << pp.pid_;
        } else {
          // TODO: maybe set another promise?
          accepted = false;
          accepted_pp = existed_pp.get();
          pp.status_ = kPaxosState_PROMISED;
          errcode = kErrCode_WRITE_PLOG_FAIL;
          LOG_ERR << "renew accepted value failed, pinst:" << pinst
                  << ", entry:" << entry << ", pid:" << pp.pid_;
        }
      } else {
        // attempt to update chosen value, reject it.
        accepted = false;
        accepted_pp = existed_pp.get();
        errcode = kErrCode_INVALID_PROPOSAL_REQ;
        LOG_ERR << "renew chosen value is not allowed, pinst:" << pinst
                << ", entry:" << entry << ", pid:" << pp.pid_;
      }
    } else {
      // delayed messages or invalid value id, reject it
      // or try to write to entry that has accepted a value, which mean remote proposer is lag behind.
      // in latter case, proposer will fail, but gathering peer chosen/accept info will ensure next write
      // from master will trigger a catchup, since the entry info from the proposal is larger than the acceptor.
      accepted = false;
      accepted_pp = existed_pp.get();
      errcode = kErrCode_INVALID_PROPOSAL_REQ;
      LOG_ERR << "invalid accept request, pinst:" << pinst
              << ", entry:" << entry << ", pid:" << pp.pid_
              << ", value id:" << pp.value_id_
              << ", existed pid:" << existed_pp->pid_
              << ", existed valudid:" << existed_pp->value_id_;
    }
  } else if (existed_promise) {
    if (existed_promise->pid_ <= pp.pid_) {
      // in case of #0 proposal optimization.
      // existed promise id is very likely to less than accepted id.
      // in which case non-master proposer proposes before master.
      pp.status_ = kPaxosState_ACCEPTED;
      if (pmn_->SetAccepted(pp) == kErrCode_OK) {
        accepted = true;
      } else {
        pp.status_ = kPaxosState_PROMISED;
        errcode = kErrCode_WRITE_PLOG_FAIL;
      }
    }
  } else {
    // #0 proposal optimization.
    pp.status_ = kPaxosState_ACCEPTED;
    if (pmn_->SetAccepted(pp) == kErrCode_OK) {
      accepted = true;
    } else {
      errcode = kErrCode_WRITE_PLOG_FAIL;
      pp.status_ = kPaxosState_FAST_ACCEPT_FAILED;
    }
  }

  memcpy(rpp, accepted_pp, ProposalHeaderSz);

  rpp->size_ = 0;
  rpp->last_chosen_from_ = uint16_t(~0u);
  rpp->last_chosen_ = pmn_->GetMaxChosenEntry(pinst);

  rsp->errcode_ = errcode;

  if (accepted) {
    // clear promised
    pmn_->ClearPromised(pinst, entry);
    rpp->status_ = kPaxosState_ACCEPTED;
  } else {
    rpp->status_ = kPaxosState_ACCEPTED_FAILED;
  }

  return std::move(rsp);
}

std::shared_ptr<PaxosMsg> Acceptor::handleChosenReq(Proposal& pp) {
  auto pinst = pp.pinst_;
  auto entry = pp.pentry_;

  auto& ent = pmn_->GetEntryAndCreateIfNotExist(pinst, entry);
  const auto& existed_pp = ent.GetProposal();

  auto ret = AllocProposalMsg(0);
  auto rpp = GetProposalFromMsg(ret.get());

  memcpy(rpp, &pp, ProposalHeaderSz);
  rpp->size_ = 0;
  rpp->status_ = kPaxosState_CHOSEN;

  if (!existed_pp || pp.pid_ != existed_pp->pid_ || pp.value_id_ != existed_pp->value_id_) {
    rpp->status_ = kPaxosState_INVALID_PROPOSAL;
    ret->errcode_ = kErrCode_INVALID_PROPOSAL_REQ;
    LOG_ERR << "invalid chosen request, proposal has change, pinst:" << pinst
            << ", entry:" << entry << ", req pid:" << pp.pid_
            << ", local pid:" << (existed_pp ? existed_pp->pid_ : ~0)
            << ", req vid:" << pp.value_id_
            << ", local vid:" << (existed_pp ? existed_pp->value_id_ : ~0);
    return ret;
  }

  if (existed_pp->status_ == kPaxosState_CHOSEN) {
    rpp->status_ = kPaxosState_ALREADY_CHOSEN;
    return ret;
  }

  existed_pp->status_ = kPaxosState_CHOSEN;
  auto err = pmn_->SetChosen(pinst, entry);

  rpp->last_chosen_from_ = uint16_t(~0u);
  rpp->last_chosen_ = pmn_->GetMaxChosenEntry(pinst);

  if (err != kErrCode_OK) {
    ret->errcode_ = kErrCode_WRITE_PLOG_FAIL;
    rpp->status_ = kPaxosState_COMMIT_FAILED;
    existed_pp->status_ = kPaxosState_ACCEPTED;
    LOG_ERR << "commit entry failed, pinst:" << pinst << ", entry:" << entry
            << ", err:" << err << ", pid:" << pp.pid_
            << ", vid:" << pp.value_id_;
  }

  return ret;
}

}  // namespace quarrel
