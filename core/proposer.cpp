#include "proposer.h"
#include "logger.h"

#include <atomic>
#include <assert.h>

namespace quarrel {

struct BatchRpcContext {
  int ret_;
  WaitGroup wg_;
  std::atomic<int> rsp_count_;
  std::shared_ptr<PaxosMsg> rsp_msg_[MAX_ACCEPTOR_NUM];

  BatchRpcContext(int expect_rsp_count)
      : wg_(expect_rsp_count), rsp_count_(0) {}
};

Proposer::Proposer(std::shared_ptr<Configure> config)
    : config_(std::move(config)) {
  auto svrid = config_->local_.id_;
  auto icount = config_->plog_inst_num_;
  auto pcount = config_->total_proposer_;

  std::unique_ptr<PaxosGroupBase> mapper;

  if (pcount == 3) {
    mapper.reset(new PaxosGroup3);
  } else if (pcount == 5) {
    mapper.reset(new PaxosGroup5);
  } else {
    assert(0);
  }

  // acceptor id of master must be the smallest of the quorum.
  // this ensure one-phase paxos will not succeed in the case of master lagged behind.

  states_.reserve(icount);
  locks_ = std::vector<std::mutex>(icount);

  for (auto i = 0ull; i < icount; i++) {
    auto pid = mapper->GetMemberIdBySvrId(i, svrid);  // proposer id
    states_.emplace_back(pid + 1, pcount);
  }
}

std::shared_ptr<PaxosMsg> Proposer::allocPaxosMsg(uint64_t pinst, uint64_t opaque, uint32_t value_size) {
  auto pm = AllocProposalMsg(value_size);
  if (!pm) return NULL;

  auto& state = states_[pinst];

  auto pid = state.ig_.GetAndInc();
  auto entry = state.last_chosen_entry_ + 1;

  pm->from_ = config_->local_.id_;
  pm->type_ = kMsgType_PREPARE_REQ;
  pm->version_ = config_->msg_version_;

  auto pp = reinterpret_cast<Proposal*>(pm->data_);

  // pp->term_ = xxxxx;
  pp->pid_ = pid;
  pp->plid_ = pinst;
  pp->pentry_ = entry;
  pp->opaque_ = opaque;
  pp->status_ = kPaxosState_PREPARED;
  pp->proposer_ = uint16_t(config_->local_.id_);

  pp->value_id_ = state.vig_.GetAndInc();
  pp->last_chosen_ = state.last_chosen_entry_;
  pp->last_chosen_from_ = uint16_t(state.last_chosen_from_);

  return pm;
}

int Proposer::Propose(uint64_t opaque, const std::string& val, uint64_t pinst) {
  assert(conn_);

  pinst = pinst % locks_.size();
  std::lock_guard<std::mutex> l(locks_[pinst]);

  auto pm = allocPaxosMsg(pinst, opaque, uint32_t(val.size()));
  if (!pm) return kErrCode_OOM;

  int ret = kErrCode_OK;
  auto pp = reinterpret_cast<Proposal*>(pm->data_);
  auto entry = pp->pentry_;

  // don't send value for prepare,not necessary.
  // memcpy(pp->data_, val.data(), val.size());

  pp->size_ -= uint32_t(val.size());
  pm->size_ -= uint32_t(val.size());

  if (val.empty() || !canSkipPrepare(*pp)) {
    // empty val indicates a read probe which must always perform prepare()
    ret = doPrepare(pm);
  }

  if (ret != kErrCode_OK && ret != kErrCode_PREPARE_PEER_VALUE) {
    LOG_ERR << "do prepare failed(" << ret << "), pinst:" << pinst
            << ", entry:" << pp->pentry_ << ", opaque:" << opaque;
    return ret;
  }

  pp = reinterpret_cast<Proposal*>(pm->data_);

  // set value
  if (ret != kErrCode_PREPARE_PEER_VALUE) {
    pp->size_ += uint32_t(val.size());
    pm->size_ += uint32_t(val.size());
    memcpy(pp->data_, val.data(), val.size());
  } else {
    assert(pp->size_);
  }

  if (pp->size_ == 0) {
    // read probe
    return ret;
  }

  pm->from_ = config_->local_.id_;
  pm->type_ = kMsgType_ACCEPT_REQ;
  pp->status_ = kPaxosState_ACCEPTED;
  pp->last_chosen_ = states_[pinst].last_chosen_entry_;
  pp->last_chosen_from_ = uint16_t(states_[pinst].last_chosen_from_);

  auto ret2 = doAccept(pm);

  if (ret2 == kErrCode_OK) {
    uint32_t size = pp->size_;
    pm->type_ = kMsgType_CHOSEN_REQ;
    pp->status_ = kPaxosState_CHOSEN;
    pp->size_ = 0;  // chosen req doesn't need to have value
    pm->size_ -= size;

    UpdateChosenInfo(pinst, entry, pp->proposer_);

    doChosen(pm);
  } else {
    ret = ret2;
  }

  return ret;
}

bool Proposer::UpdateLocalStateFromRemoteMsg(std::shared_ptr<PaxosMsg>& m) {
  auto pp = GetProposalFromMsg(m.get());
  UpdatePrepareId(pp->plid_, pp->pid_);
  UpdateChosenInfo(pp->plid_, pp->last_chosen_, pp->last_chosen_from_);
  return true;
}

bool Proposer::UpdatePrepareId(uint64_t pinst, uint64_t pid) {
  auto& state = states_[pinst];
  state.ig_.SetGreaterThan(pid);
  return true;
}

bool Proposer::UpdateChosenInfo(uint64_t pinst, uint64_t chosen, uint64_t from) {
  auto& state = states_[pinst];

  if (chosen < state.last_chosen_entry_) {
    LOG_ERR << "local chosen entry > new chosen, pinst:" << pinst << ", current chosen:"
            << state.last_chosen_entry_ << ", new chosen:" << chosen << ", from:" << from;
    return false;
  }

  state.ig_.Reset(state.proposer_id_);

  state.last_chosen_entry_ = chosen;
  state.last_chosen_from_ = uint32_t(from);
  return true;
}

int Proposer::onChosenNotify(std::shared_ptr<PaxosMsg> msg) {
  auto pp = GetProposalFromMsg(msg.get());
  std::lock_guard<std::mutex> l(locks_[pp->plid_]);
  UpdateChosenInfo(pp->plid_, pp->pentry_, pp->proposer_);
  return 0;
}

bool Proposer::canSkipPrepare(const Proposal& pp) {
  //1. proposer of last entry(which consensus is reached)
  //2. #0 proposal for current entry.
  auto pinst = pp.plid_;
  auto entry = pp.pentry_;
  const auto& state = states_[pinst % states_.size()];

  if (state.last_chosen_entry_ == entry - 1 && state.last_chosen_from_ == state.proposer_id_ && pp.pid_ == state.proposer_id_ + 1) {
    return true;
  }

  return false;
}

std::shared_ptr<BatchRpcContext> Proposer::doBatchRpcRequest(
    int majority, std::shared_ptr<PaxosMsg>& pm) {
  // shared objects must be put on heap.
  // so that when a delayed msg arrives, there won't be any memory access
  // violation
  auto ctx = std::make_shared<BatchRpcContext>(majority);

  auto cb = [ctx](std::shared_ptr<PaxosMsg> msg) -> int {
    // delayed rsp will be ignored.
    auto idx = ctx->rsp_count_.fetch_add(1);
    ctx->rsp_msg_[idx] = std::move(msg);
    ctx->wg_.Notify();
    return 0;
  };

  ctx->ret_ = kErrCode_OK;
  auto pinst = GetPLIdFromMsg(pm.get());
  auto& local = conn_->GetLocalConn();
  auto& remote = conn_->GetRemoteConn(pinst);

  RpcReqData req{config_->timeout_, std::move(cb), pm};

  auto ret = 0;
  if ((ret = local->DoRpcRequest(req))) {
    ctx->ret_ = ret;
    return std::move(ctx);
  }

  for (auto i = 0u; i < remote.size(); ++i) {
    remote[i]->DoRpcRequest(req);
  }

  if (!ctx->wg_.Wait(config_->timeout_)) {
    ctx->ret_ = kErrCode_TIMEOUT;
  }

  return ctx;
}

int Proposer::doPrepare(std::shared_ptr<PaxosMsg>& pm) {
  // send to local conn
  // then to remote

  uint32_t valid_rsp = 0;
  std::shared_ptr<PaxosMsg> last_voted;
  auto majority = config_->total_acceptor_ / 2 + 1;
  auto origin_proposal = reinterpret_cast<Proposal*>(pm->data_);

  auto ctx = doBatchRpcRequest(majority, pm);
  if (ctx->ret_ != kErrCode_OK) return ctx->ret_;

  for (auto idx = 0; idx < ctx->rsp_count_; ++idx) {
    std::shared_ptr<PaxosMsg> m = std::move(ctx->rsp_msg_[idx]);
    auto rsp_proposal = reinterpret_cast<Proposal*>(m->data_);

    if (m->type_ != kMsgType_PREPARE_RSP) {
      LOG_ERR << "invalid prepare rsp type from peer, type:" << m->type_
              << ", from:" << m->from_;
      continue;
    }

    if (rsp_proposal->status_ != kPaxosState_PROMISED) {
      LOG_ERR << "peer failed to promise, status:" << rsp_proposal->status_
              << ", from:" << m->from_ << " @(" << rsp_proposal->plid_ << ","
              << rsp_proposal->pentry_ << ")";
      continue;
    }

    if (rsp_proposal->last_chosen_ > 0) {
      UpdateChosenInfo(rsp_proposal->plid_, rsp_proposal->last_chosen_, rsp_proposal->last_chosen_from_);
    }

    if (rsp_proposal->pid_ > origin_proposal->pid_) {
      // rejected
      UpdatePrepareId(origin_proposal->plid_, rsp_proposal->pid_);
      continue;
    }

    if (rsp_proposal->value_id_ != origin_proposal->value_id_ ||
        rsp_proposal->opaque_ != origin_proposal->opaque_) {
      // peer responses with last vote

      LOG_INFO << "peer return last vote, from:" << m->from_
               << ", pinst@entry:" << rsp_proposal->plid_ << "@"
               << rsp_proposal->pentry_ << ", pid:" << rsp_proposal->pid_
               << ", vid:" << rsp_proposal->value_id_
               << ", vsize:" << rsp_proposal->size_;

      assert(rsp_proposal->size_ > 0);

      auto lastp = reinterpret_cast<Proposal*>(last_voted->data_);
      if (last_voted.get() == NULL || lastp->pid_ < rsp_proposal->pid_) {
        // last vote with the largest prepare id

        // NOTE: no majority is required(maybe we should)
        // consistency is maintained, but this value come from nowhere may
        // surprise user.
        last_voted = std::move(m);
      }
    }

    ++valid_rsp;
  }

  if (valid_rsp >= majority) {
    if (last_voted) {
      pm = std::move(last_voted);
      return kErrCode_PREPARE_PEER_VALUE;
    }
    return kErrCode_OK;
  }

  return kErrCode_PREPARE_NOT_QUORAUM;
}

int Proposer::doAccept(std::shared_ptr<PaxosMsg>& pm) {
  // send to local conn
  // then to remote
  auto majority = config_->total_acceptor_ / 2 + 1;
  auto origin_proposal = reinterpret_cast<Proposal*>(pm->data_);

  auto ctx = doBatchRpcRequest(majority, pm);
  if (ctx->ret_ != kErrCode_OK) return ctx->ret_;

  uint32_t valid_rsp = 0;
  for (auto idx = 0; idx < ctx->rsp_count_; ++idx) {
    std::shared_ptr<PaxosMsg> m = std::move(ctx->rsp_msg_[idx]);
    auto rsp_proposal = reinterpret_cast<Proposal*>(m->data_);

    if (m->type_ != kMsgType_ACCEPT_RSP) {
      LOG_ERR << "invalid accept rsp type from peer, type:" << m->type_
              << ", from:" << m->from_;
      continue;
    }

    if (rsp_proposal->last_chosen_ > 0) {
      UpdateChosenInfo(rsp_proposal->plid_, rsp_proposal->last_chosen_, rsp_proposal->last_chosen_from_);
    }

    if (rsp_proposal->status_ != kPaxosState_ACCEPTED) {
      UpdatePrepareId(origin_proposal->plid_, rsp_proposal->pid_);
      LOG_ERR << "peer failed to accept, status:" << rsp_proposal->status_
              << ", from:" << m->from_ << " @(" << rsp_proposal->plid_ << ","
              << rsp_proposal->pentry_ << ")";
      continue;
    }

    if (rsp_proposal->pid_ > origin_proposal->pid_) {
      // rejected
      UpdatePrepareId(origin_proposal->plid_, rsp_proposal->pid_);
      continue;
    }

    if (rsp_proposal->value_id_ != origin_proposal->value_id_ ||
        rsp_proposal->opaque_ != origin_proposal->opaque_) {
      // not possible without fast-accept enabled.
      LOG_ERR << "invalid doAccept rsp from peer(" << pm->from_
              << "), value id is changed, rsp:(" << rsp_proposal->value_id_
              << "," << rsp_proposal->opaque_ << "), origin:("
              << origin_proposal->value_id_ << "," << rsp_proposal->opaque_
              << ")";
      continue;
    }

    ++valid_rsp;
  }

  if (valid_rsp >= majority) {
    return kErrCode_OK;
  }

  return kErrCode_ACCEPT_NOT_QUORAUM;
}

int Proposer::doChosen(std::shared_ptr<PaxosMsg>& pm) {
  // broadcast to peers
  doBatchRpcRequest(0, pm);
  return kErrCode_OK;
}
}  // namespace quarrel
