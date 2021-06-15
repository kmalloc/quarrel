#include "proposer.h"
#include "logger.h"
#include "idgen.hpp"

#include <atomic>
#include <assert.h>

namespace quarrel {

struct InstanceState {
  // in-memory state of each paxos instance.
  // the very first proposal for each instance at startup may require a second try.
  // since the first try will fail if last chosen entry is not recovered correctly from disk(which is allowed).
  // same logic applys for proposing from slave for the moment
  // a potential optimization for this particular case(write from slave) can be achieved by receiving chosen msg from acceptor.
  uint32_t proposer_id_;  // proposer id for current proposer, this is varied for each instance.
  uint32_t last_chosen_from_;
  uint64_t last_chosen_entry_;

  uint64_t term_{0};  // logical time

  // idgen will be reset when the latest entry is chosen
  IdGen ig_;         // proposal id generator
  IdGenByDate vig_;  // value id generator

  uint8_t state_;  // highest bit indicates whether client is notified(early return in the case of accepting peer value from prepare).
  uint8_t rsp_count_{0};
  uint8_t valid_rsp_count_{0};

  std::promise<int> ret_;  // return code to caller

  // ongoing req for acceptors
  std::shared_ptr<PaxosMsg> req_;

  // largest last vote from peers during prepare phase
  std::shared_ptr<PaxosMsg> lastv_;

  InstanceState() : proposer_id_(0), last_chosen_from_(~0u), last_chosen_entry_(0), ig_(0, 0), vig_(0xff, 1) {}

  InstanceState(int pid, int proposer_count)
      : proposer_id_(pid), last_chosen_from_(~0u), last_chosen_entry_(0), ig_(pid + 1, proposer_count), vig_(0xff, 1) {}
};

Proposer::Proposer(std::shared_ptr<Configure> config)
    : config_(std::move(config)), timeout_(config_->timeout_ - 1), wpool_([this](RpcResponseMsg m) { return handleRpcResponse(std::move(m)); }) {
  auto svrid = config_->local_.id_;
  auto icount = config_->plog_inst_num_;
  auto pcount = config_->total_proposer_;

  auto mapper = PaxosGroupBase::CreateGroup(config_->total_proposer_);

  states_.reset(new InstanceState[icount]);
  locks_ = std::vector<std::mutex>(icount);

  for (auto i = 0ull; i < icount; i++) {
    auto pid = mapper->GetMemberIdBySvrId(i, svrid);  // proposer id
    states_[i] = InstanceState(pid, pcount);
  }

  timeout_.Start();
  wpool_.StartWorker(config_->proposer_worker_count_, config_->worker_msg_queue_sz_);
}

Proposer::~Proposer() {
  timeout_.Stop();
  wpool_.StopWorker();
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
    pid = state.ig_.GetAndInc() | config_->pid_cookie_;
  }

  pm->from_ = config_->local_.id_;  // this is server id not proposer id.
  pm->type_ = kMsgType_PREPARE_REQ;
  pm->version_ = config_->msg_version_;

  auto pp = GetProposalFromMsg(pm.get());

  pp->pid_ = pid;
  pp->plid_ = pinst;
  pp->pentry_ = entry;
  pp->opaque_ = opaque;
  pp->status_ = kPaxosState_PREPARED;
  pp->proposer_ = uint16_t(state.proposer_id_);

  pp->term_ = state.term_++;
  pp->value_id_ = state.vig_.GetAndInc();
  pp->last_chosen_ = state.last_chosen_entry_;
  pp->last_chosen_from_ = uint16_t(state.last_chosen_from_);

  return pm;
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

int Proposer::OnEntryChosen(std::shared_ptr<PaxosMsg> msg, bool from_plog) {
  auto pp = GetProposalFromMsg(msg.get());

  auto pinst = pp->plid_ % locks_.size();
  std::lock_guard<std::mutex> l(locks_[pinst]);

  // external chosen notify from plog is only allowed for master.
  // to make sure that slave will not allow one-phase proposing after recover.

  if (from_plog && states_[pinst].proposer_id_ != 0) {
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
  const auto& state = states_[pinst % locks_.size()];

  // acceptor id of master must be the smallest of the synod(set of acceptors).
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

  if (state.last_chosen_entry_ == entry - 1 && state.last_chosen_from_ == state.proposer_id_ && pp.pid_ == state.proposer_id_ + 1) {
    return true;
  }

  return false;
}

int Proposer::handleRpcResponse(RpcResponseMsg m) {
  auto term = m.term_;
  auto pinst = m.pinst_ % locks_.size();

  std::lock_guard<std::mutex> l(locks_[pinst]);

  auto& state = states_[pinst];
  if (term != state.term_ - 1 || !state.req_) {
    // timeout or delayed msg arrives after majority of responses been handled
    return kErrCode_OK;
  }

  if (!m.msg_) {
    state.req_.reset();
    state.lastv_.reset();
    if ((state.state_ & 0x80) == 0) {
      state.ret_.set_value(kErrCode_TIMEOUT);
    }
  } else {
    if ((state.state_ & 0x7f) == kPaxosState_PREPARING /*|| m.msg_->type_ == kMsgType_PREPARE_RSP*/) {
      // state will change to accepting when majority rsp been handled.
      // those late comer should should just skipped.
      return handlePrepareRsp(std::move(m.msg_));
    } else if ((state.state_ & 0x7f) == kPaxosState_ACCEPTING) {
      return handleAcceptRsp(std::move(m.msg_));
    } else {
      // TODO report delayed msg
    }
  }
  return kErrCode_OK;
}

int Proposer::addRpcResponse(uint64_t pinst, uint64_t entry, std::shared_ptr<PaxosMsg> m, uint64_t term) {
  RpcResponseMsg msg;
  msg.term_ = term;
  msg.pinst_ = pinst;
  msg.entry_ = entry;
  msg.msg_ = std::move(m);

  if (wpool_.AddWork(pinst, std::move(msg))) {
    return kErrCode_PROPOSER_QUEUE_FULL;
  }

  return kErrCode_OK;
}

int Proposer::doBatchRpcRequest(std::shared_ptr<PaxosMsg>& pm) {
  auto pinst = GetPLIdFromMsg(pm.get());
  auto& local = conn_->GetLocalConn();
  auto& remote = conn_->GetRemoteConn(pinst);

  auto pp = GetProposalFromMsg(pm.get());
  auto entry = pp->pentry_;
  auto term = pp->term_;

  auto cb = [this, pinst, entry, term](std::shared_ptr<PaxosMsg> msg) -> int {
    // delayed rsp will be ignored.
    addRpcResponse(pinst, entry, std::move(msg), term);
    return 0;
  };

  RpcReqData req{config_->timeout_, std::move(cb), pm};

  if (local->DoRpcRequest(req)) {
    return kErrCode_CONN_FAIL;
  }

  for (auto i = 0u; i < remote.size(); ++i) {
    remote[i]->DoRpcRequest(req);
  }

  return kErrCode_OK;
}

void Proposer::setupWaitState(uint64_t pinst, uint64_t entry, uint32_t state, std::promise<int>* promise, std::shared_ptr<PaxosMsg>* pm) {
  states_[pinst].rsp_count_ = 0;
  states_[pinst].valid_rsp_count_ = 0;
  states_[pinst].state_ = uint8_t(state);

  if (promise) {
    states_[pinst].ret_ = std::move(*promise);
  }

  if (pm) {
    states_[pinst].req_ = std::move(*pm);
    auto pp = GetProposalFromMsg(states_[pinst].req_.get());

    auto term = pp->term_;

    TimingWheelNotifier tm_notify = [=, &tm_notify](uint64_t opaque) {
      assert(term == opaque);
      if (addRpcResponse(pinst, entry, NULL, term) != kErrCode_OK) {
        // timeout event must be handled
        timeout_.AddTimeout(term, config_->timeout_, tm_notify);
      }
    };

    if (!timeout_.AddTimeout(term, 2 * config_->timeout_, tm_notify)) {
      states_[pinst].ret_.set_value(kErrCode_SETUP_TIMEOUT_FAIL);
      LOG_ERR << "setup timeout failed, pinst:" << pinst << ", entry:" << entry
              << ", state:" << state << ", term:" << term;
    }
  }
}

int Proposer::Propose(uint64_t opaque, const std::string& val, uint64_t pinst) {
  auto ft = ProposeAsync(opaque, val, pinst);
  ft.wait();
  return ft.get();
}

std::future<int> Proposer::ProposeAsync(uint64_t opaque, const std::string& val, uint64_t pinst) {
  assert(conn_);

  std::promise<int> frt;
  auto retf = frt.get_future();

  pinst = pinst % locks_.size();
  std::lock_guard<std::mutex> l(locks_[pinst]);

  if (states_[pinst].req_) {
    frt.set_value(kErrCode_WORKING_IN_PROPGRESS);
    return retf;
  }

  auto pm = allocPaxosMsg(pinst, opaque, uint32_t(val.size()));

  if (!pm) {
    frt.set_value(kErrCode_OOM);
    return retf;
  }

  int ret = kErrCode_OK;
  auto pp = GetProposalFromMsg(pm.get());

  memcpy(pp->data_, val.data(), val.size());

  if (val.empty() || !canSkipPrepare(*pp)) {
    // empty val indicates a read probe which must always perform prepare()
    pp->size_ -= uint32_t(val.size());
    pm->size_ -= uint32_t(val.size());
    auto pm2 = CloneProposalMsg(*pm);
    pp->size_ += uint32_t(val.size());
    pm->size_ += uint32_t(val.size());

    ret = doPrepare(pm2);
  }

  if (ret == kErrCode_WORKING_IN_PROPGRESS) {
    setupWaitState(pinst, pp->pentry_, kPaxosState_PREPARING, &frt, &pm);
    return retf;
  }

  if (ret != kErrCode_OK) {
    frt.set_value(ret);
    LOG_ERR << "do prepare failed(" << ret << "), pinst:" << pinst
            << ", entry:" << pp->pentry_ << ", vsz:" << val.size() << ", opaque:" << opaque;
    return retf;
  }

  pm->from_ = config_->local_.id_;
  pm->type_ = kMsgType_ACCEPT_REQ;
  pp->status_ = kPaxosState_ACCEPTED;
  pp->last_chosen_ = states_[pinst].last_chosen_entry_;
  pp->last_chosen_from_ = uint16_t(states_[pinst].last_chosen_from_);

  auto ret2 = doAccept(pm);

  if (ret2 == kErrCode_WORKING_IN_PROPGRESS) {
    setupWaitState(pinst, pp->pentry_, kPaxosState_ACCEPTING, &frt, &pm);
    return retf;
  }

  frt.set_value(ret2);
  return retf;
}

int Proposer::handlePrepareRsp(std::shared_ptr<PaxosMsg> rsp) {
  auto rsp_proposal = GetProposalFromMsg(rsp.get());
  auto pinst = rsp_proposal->plid_;

  if (!states_[pinst].req_) {
    // delayed rsp, TODO: update monitor
    return 0;
  }

  bool done = false;
  bool first_got_lastv = false;
  bool should_do_accept = false;
  bool has_last_vote = (states_[pinst].state_ & 0x80);

  auto ret = rsp->errcode_;
  auto majority = config_->total_acceptor_ / 2 + 1;

  auto& req_pm = states_[pinst].req_;
  auto pp = GetProposalFromMsg(rsp.get());
  auto req_proposal = GetProposalFromMsg(req_pm.get());

  if (rsp->type_ != kMsgType_PREPARE_RSP) {
    ret = -1;
    LOG_ERR << "invalid prepare rsp type from peer, pinst:" << pinst
            << ",entry:" << rsp_proposal->pentry_ << "type:" << rsp->type_
            << ", from:" << rsp->from_;
    goto out;
  }

  states_[pinst].rsp_count_++;

  if (states_[pinst].rsp_count_ == config_->total_acceptor_) {
    done = true;
  }

  if (ret != kErrCode_OK) {
    LOG_ERR << "peer return err for prepare, ret:" << ret << ", status:" << rsp_proposal->status_
            << ", from:" << rsp->from_ << " @(" << rsp_proposal->plid_ << ","
            << rsp_proposal->pentry_ << ")";
    goto out;
  }

  if (rsp_proposal->status_ != kPaxosState_PROMISED) {
    ret = -2;
    LOG_ERR << "peer failed to promise, status:" << rsp_proposal->status_
            << ", from:" << rsp->from_ << " @(" << rsp_proposal->plid_ << ","
            << rsp_proposal->pentry_ << ")";
    goto out;
  }

  CopyProposalMeta(*req_proposal, *rsp_proposal);

  if (rsp_proposal->pid_ > req_proposal->pid_) {
    ret = -3;
    UpdatePrepareId(req_proposal->plid_, rsp_proposal->pid_);
    LOG_ERR << "peer rejected to promise, status:" << rsp_proposal->status_
            << ", from:" << rsp->from_ << " @(" << rsp_proposal->plid_ << ","
            << rsp_proposal->pentry_ << "), req pid:" << req_proposal->pid_ << ", rsp pid:" << rsp_proposal->pid_;
    goto out;
  }

  if (rsp_proposal->value_id_ != req_proposal->value_id_) {
    // peer responses with last accepted vote(may not be chosen yet)

    // note that last vote that is not chosen(maybe just been accepted by one member) is not guaranteed to be selected here.
    // consider a 3 member synod, A/B/C，where C has an accepted value va.
    // proposer p1 sends prepare to A/B/C， A/B responses come before C's.
    // then p1 considers a majority valid responses has been gathered, then comes to phase 2.
    // so to guarantee accepted value will be selected, proposer must wait until all acceptors has responsed.
    // this is not necessary but no harm, at the cost of a little performance panalty.

    // current implementation won't do that, and thus introduces a little randomness.

    auto& last_voted = states_[pinst].lastv_;
    auto lastp = GetProposalFromMsg(last_voted.get());

    LOG_INFO << "peer return last vote, from:" << rsp->from_
             << ", pinst@entry(" << rsp_proposal->plid_ << "," << rsp_proposal->pentry_ << ")"
             << ", req pid:" << req_proposal->pid_
             << ", rsp pid:" << rsp_proposal->pid_
             << ", req vid:" << req_proposal->value_id_
             << ", rsp vid:" << rsp_proposal->value_id_;

    assert(rsp_proposal->size_ > 0);

    if (!has_last_vote || lastp->pid_ < rsp_proposal->pid_) {
      // last vote with the largest prepare id

      if (!has_last_vote) {
        first_got_lastv = true;
        states_[pinst].state_ |= 0x80;
      }

      // NOTE: no majority is required(maybe we should)
      // consistency is maintained, but this value come from nowhere may surprise user.

      has_last_vote = true;
      auto npm = CloneProposalMsg(*rsp);
      auto npp = GetProposalFromMsg(npm.get());

      // allocate an new pid
      // npp->pid_ = states_[pinst].ig_.GetAndInc();

      last_voted = std::move(npm);

      LOG_ERR << "select peer value from prepare rsp, from:" << last_voted->from_
              << ", pinst:" << pinst << ", entry:" << pp->pentry_ << ", new pid:" << npp->pid_;
    }
  }

  states_[pinst].valid_rsp_count_++;
  should_do_accept = (states_[pinst].valid_rsp_count_ >= majority);

  if (has_last_vote && should_do_accept) {
    // we done't really need this.
    // but by doing so will reduce a little randomness and make testing easier.
    should_do_accept = done;
  }

  if (should_do_accept) {
    int ret2 = kErrCode_OK;
    if (pp->pid_ > 0) {
      auto& m = has_last_vote ? states_[pinst].lastv_ : req_pm;

      pp = GetProposalFromMsg(m.get());
      m->from_ = config_->local_.id_;
      m->type_ = kMsgType_ACCEPT_REQ;

      pp->status_ = kPaxosState_ACCEPTED;
      pp->last_chosen_ = states_[pinst].last_chosen_entry_;
      pp->last_chosen_from_ = uint16_t(states_[pinst].last_chosen_from_);

      ret2 = doAccept(m);
      states_[pinst].lastv_.reset();
    } else {
      // pid == 0 indicates read probe
    }

    if (ret2 == kErrCode_WORKING_IN_PROPGRESS) {
      done = false;
      uint8_t returned = (states_[pinst].state_ & 0x80);
      setupWaitState(pinst, pp->pentry_, kPaxosState_ACCEPTING | returned, NULL, NULL);
    } else {
      ret = ret2;
      done = true;
    }
  } else if (done) {
    // TODO: maybe notify lag-behind acceptor to do catchup here asynchronously.
    // in case of local acceptor lag behind, first write attempt will fail.
    // then local states last_chosen_ will be updated to globally max chosen(information gathered from other acceptors)
    // a second attempt to write to local acceptor will trigger that acceptor to do catchup.
    // so explicit catchup notification from proposer is not necessary but no harm.
  }

out:
  if (first_got_lastv && (states_[pinst].state_ & 0x80)) {
    // at this point we know propose() will fail eventually.
    // early return to client.
    states_[pinst].ret_.set_value(kErrCode_PREPARE_PEER_VALUE);
  }

  if (done) {
    if (ret < 0) {
      ret = kErrCode_PREPARE_NOT_QUORAUM;
    }

    states_[pinst].req_.reset();

    if ((states_[pinst].state_ & 0x80) == 0) {
      states_[pinst].ret_.set_value(ret);
    }
  }

  return ret;
}

int Proposer::doPrepare(std::shared_ptr<PaxosMsg>& pm) {
  auto ret = doBatchRpcRequest(pm);
  if (ret != kErrCode_OK) return ret;

  return kErrCode_WORKING_IN_PROPGRESS;
}

int Proposer::handleAcceptRsp(std::shared_ptr<PaxosMsg> rsp) {
  auto rsp_proposal = GetProposalFromMsg(rsp.get());
  auto pinst = rsp_proposal->plid_;

  if (!states_[pinst].req_) {
    // delayed rsp, TODO: update monitor
    return 0;
  }

  bool done = false;
  auto ret = rsp->errcode_;
  uint32_t size = rsp_proposal->size_;
  auto majority = config_->total_acceptor_ / 2 + 1;

  auto& req_pm = states_[pinst].req_;
  auto req_proposal = GetProposalFromMsg(req_pm.get());

  if (rsp->type_ != kMsgType_ACCEPT_RSP) {
    ret = kErrCode_INVALID_ACCEPT_RSP;
    LOG_ERR << "late comer or invalid accept rsp type from peer, pinst:" << pinst
            << ", entry:" << rsp_proposal->pentry_ << ", type:" << rsp->type_ << ", from:" << rsp->from_;
    goto out;
  }

  states_[pinst].rsp_count_++;

  if (states_[pinst].rsp_count_ == config_->total_acceptor_) {
    done = true;
  }

  if (ret != kErrCode_OK) {
    goto out;
  }

  if (rsp_proposal->status_ != kPaxosState_ACCEPTED) {
    ret = kErrCode_PEER_ACCEPT_FAIL;
    LOG_ERR << "peer failed to accept, status:" << rsp_proposal->status_
            << ", from:" << rsp->from_ << " @(" << rsp_proposal->plid_ << ","
            << rsp_proposal->pentry_ << ")";
    goto out;
  }

  CopyProposalMeta(*req_proposal, *rsp_proposal);

  if (rsp_proposal->pid_ != req_proposal->pid_) {
    // rejected
    ret = kErrCode_PEER_ACCEPT_REJECTED;
    UpdatePrepareId(req_proposal->plid_, rsp_proposal->pid_);
    goto out;
  }

  if (rsp_proposal->value_id_ != req_proposal->value_id_) {
    // not possible without fast-accept enabled.
    ret = kErrCode_PEER_ACCEPT_OTHER_VALUE;
    LOG_ERR << "invalid doAccept rsp from peer(" << rsp->from_
            << "), value id is changed, rsp:(" << rsp_proposal->value_id_
            << "," << rsp_proposal->opaque_ << "), origin:("
            << req_proposal->value_id_ << "," << rsp_proposal->opaque_
            << ")";
    goto out;
  }

  states_[pinst].valid_rsp_count_++;

  if (states_[pinst].valid_rsp_count_ >= majority) {
    done = true;
    auto pp = GetProposalFromMsg(rsp.get());

    rsp->size_ -= size;
    rsp->from_ = config_->local_.id_;
    rsp->type_ = kMsgType_CHOSEN_REQ;
    pp->size_ = 0;  // chosen req doesn't need to have value
    pp->status_ = kPaxosState_CHOSEN;

    doChosen(rsp);
    UpdateChosenInfo(pinst, pp->pentry_, pp->proposer_);
  }

out:

  if (done) {
    states_[pinst].req_.reset();
    if ((states_[pinst].state_ & 0x80) == 0) {
      states_[pinst].ret_.set_value(ret);
    }
  }

  return ret;
}

int Proposer::doAccept(std::shared_ptr<PaxosMsg>& pm) {
  auto ret = doBatchRpcRequest(pm);
  if (ret != kErrCode_OK) return ret;

  return kErrCode_WORKING_IN_PROPGRESS;
}

int Proposer::doChosen(std::shared_ptr<PaxosMsg>& pm) {
  // broadcast to peers
  doBatchRpcRequest(pm);
  return kErrCode_OK;
}
}  // namespace quarrel
