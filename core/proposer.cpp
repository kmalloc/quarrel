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

  auto mapper = PaxosGroupBase::CreateGroup(config_->total_proposer_);

  states_.reserve(icount);
  locks_ = std::vector<std::mutex>(icount);

  for (auto i = 0ull; i < icount; i++) {
    auto pid = mapper->GetMemberIdBySvrId(i, svrid);  // proposer id
    states_.emplace_back(pid, pcount);
  }
}

std::shared_ptr<PaxosMsg> Proposer::allocPaxosMsg(uint64_t pinst, uint64_t opaque, uint32_t value_size) {
  auto pm = AllocProposalMsg(value_size);
  if (!pm) return NULL;

  auto& state = states_[pinst];

  auto pid = 0ull;
  auto entry = state.last_chosen_entry_ + 1;

  if (value_size) {
    // value size == 0 indicates a read probe.
    // read probe must not consume proposal id.
    pid = state.ig_.GetAndInc();
  }

  pm->from_ = config_->local_.id_;  // this is server id not proposer id.
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

  if (chosen == 0 || chosen == ~0ull) {
    return false;
  }

  if (chosen < state.last_chosen_entry_) {
    LOG_ERR << "local chosen entry > new chosen, pinst:" << pinst << ", current chosen:"
            << state.last_chosen_entry_ << ", new chosen:" << chosen << ", from:" << from;
    return false;
  }

  /*
  LOG_ERR << "local chosen entry updated, pinst:" << pinst << ", current chosen:"
          << state.last_chosen_entry_ << ", new chosen:" << chosen << ", from:" << from;
          */

  state.ig_.Reset(state.proposer_id_ + 1);

  state.last_chosen_entry_ = chosen;
  state.last_chosen_from_ = uint32_t(from);

  return true;
}

int Proposer::onChosenNotify(std::shared_ptr<PaxosMsg> msg) {
  auto pp = GetProposalFromMsg(msg.get());

  auto pinst = pp->plid_ % locks_.size();
  std::lock_guard<std::mutex> l(locks_[pinst]);

  // external chosen notify is only allowed for master.
  // to make sure that slave will not allow one-phase proposing after recover.

  if (states_[pinst].proposer_id_ != 0) {
    // proposer id 0 indicates master
    return -1;
  }

  UpdateChosenInfo(pinst, pp->pentry_, pp->proposer_);
  return 0;
}

bool Proposer::canSkipPrepare(const Proposal& pp) {
  //1. proposer of last entry(which consensus is reached)
  //2. #0 proposal for current entry.
  auto pinst = pp.plid_;
  auto entry = pp.pentry_;
  const auto& state = states_[pinst % states_.size()];

  // acceptor id of master must be the smallest of the quorum.
  // this ensure one-phase paxos will not succeed in the case of lagged-behind master try to propose to an entry which already has accepted value in remote peers.
  // consider following case
  // master: |v1|v2|| slave1: |v1|v2|v3| slave2: |v1|v2|v3|
  // v1/v2 is proposed by master, then master crashed, v3 is proposed by slave1, and reach majority consensus.
  // then master recover, and trys to propose v4 to entry 3.
  // at this moment master still considers itself master(because v2 is proposed by itself), and try one-phased proposing,
  // which will fail because the proposal id will be rejected.

  // what about slave2 proposing after recovering from crash?
  // consider following case
  // master: |v1|v2|v3| slave1: |v1|v2|v3| slave2: |v1|v2||
  // v1 is proposed by master, then for some reason, slave2 proposes v2 by two-phase proposing.
  // then slave2 crashed, master then proposed v3 using two-phase proposing and reached global consensus.
  // then slave3 recovered, slave3 finds that v2 was proposed by itself, but this time slave3 must not be allowed to use one-phase proposing.
  // so we must ensure that slave will not do one-phase proposing for its very first proposal after recovering.
  // we can do that by just setting last_chosen_from_ to an invalid value when initializing slave;

  // currently all last_chosen_from_ is initialized to 0 at startup, which means both master and slave will do 2-phase proposing for the first proposal after recovering.
  // in the case of master startup, there is room for optimization, just make sure above rule is followed.

  /*
  LOG_INFO << "check skip prepare, last chosen:" << state.last_chosen_entry_
           << ", last from:" << state.last_chosen_from_ << ", inst:" << pinst << ", entry:" << entry
           << ", proposer id:" << state.proposer_id_ << ", pid:" << pp.pid_;
           */

  if (state.last_chosen_entry_ == entry - 1 && state.last_chosen_from_ == uint32_t(config_->local_.id_) && pp.pid_ == state.proposer_id_ + 1) {
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

    CopyProposalMeta(*origin_proposal, *rsp_proposal);

    if (rsp_proposal->pid_ > origin_proposal->pid_) {
      // rejected
      UpdatePrepareId(origin_proposal->plid_, rsp_proposal->pid_);
      continue;
    }

    if (rsp_proposal->value_id_ != origin_proposal->value_id_) {
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

    if (rsp_proposal->status_ != kPaxosState_ACCEPTED) {
      UpdatePrepareId(origin_proposal->plid_, rsp_proposal->pid_);
      LOG_ERR << "peer failed to accept, status:" << rsp_proposal->status_
              << ", from:" << m->from_ << " @(" << rsp_proposal->plid_ << ","
              << rsp_proposal->pentry_ << ")";
      continue;
    }

    CopyProposalMeta(*origin_proposal, *rsp_proposal);

    if (rsp_proposal->pid_ > origin_proposal->pid_) {
      // rejected
      UpdatePrepareId(origin_proposal->plid_, rsp_proposal->pid_);
      continue;
    }

    if (rsp_proposal->value_id_ != origin_proposal->value_id_) {
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
