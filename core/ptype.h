#ifndef __QUARREL_PTYPE_H_
#define __QUARREL_PTYPE_H_

#include <memory>
#include <string>
#include <cstddef>
#include <cstdint>
#include <stdlib.h>
#include <string.h>
#include <functional>

namespace quarrel {

enum PaxosGroupType {
  PGT_Synod3 = 3,
  PGT_Synod5 = 5,
  PGT_Synod64 = 64,
};

enum PaxosState {
  kPaxosState_INIT = 0,
  kPaxosState_PREPARED = 1,
  kPaxosState_PROMISED = 2,
  kPaxosState_ACCEPTED = 3,
  kPaxosState_CHOSEN = 4,
  kPaxosState_ALREADY_CHOSEN = 5,
  kPaxosState_PROMISED_FAILED = 6,
  kPaxosState_ACCEPTED_FAILED = 7,
  kPaxosState_COMMIT_FAILED = 8,
  kPaxosState_INVALID_PROPOSAL = 9,
  kPaxosState_LOCAL_LAG_BEHIND = 10,
  kPaxosState_REMOTE_LAG_BEHIND = 11,
  kPaxosState_FAST_ACCEPT_FAILED = 12,

  // state of proposer
  kPaxosState_PREPARING = 21,
  kPaxosState_PROMISING = 22,
  kPaxosState_ACCEPTING = 23,
  kPaxosState_CHOOSING = 24,
};

enum PaxosMsgType {
  kMsgType_INVALID = 0,
  kMsgType_PREPARE_REQ = 1,
  kMsgType_PREPARE_RSP = 2,
  kMsgType_ACCEPT_REQ = 3,
  kMsgType_ACCEPT_RSP = 4,
  kMsgType_CHOSEN_REQ = 5,
  kMsgType_CHOSEN_RSP = 6,
  kMsgType_CHORE_REQ = 101,
  kMsgType_CHORE_CATCHUP = 102,
  kMsgType_INVALID_REQ = 0xffffffff,
};

enum PaxosErrCode {
  kErrCode_OK = 0,
  kErrCode_FIRST_INTERNAL_ERR = -30000,
  kErrCode_OOM = -30001,
  kErrCode_TIMEOUT = -30002,
  kErrCode_PREPARE_NOT_QUORAUM = -30003,
  kErrCode_ACCEPT_NOT_QUORAUM = -30004,
  kErrCode_CONN_FAIL = -30005,
  kErrCode_PLOG_NOT_EXIST = -30006,
  kErrCode_PREPARE_PEER_VALUE = -30007,   // prepare return peer's value
  kErrCode_INVALID_PLOG_DATA = -30008,    // invalid formated plog data
  kErrCode_UNMARSHAL_PLOG_FAIL = -30009,  // unmarshal plog fail
  kErrCode_WORKER_NOT_STARTED = -30010,
  kErrCode_WORKER_ALREADY_STARTED = -30011,
  kErrCode_PROPOSAL_NOT_EXIST = -30012,
  kErrCode_WRITE_PLOG_FAIL = -30013,
  kErrCode_INVALID_PROPOSAL_REQ = -30014,
  kErrCode_DUPLICATE_PROPOSAL_REQ = -30015,
  kErrCode_NEED_CATCHUP = -30016,
  kErrCode_ENTRY_NOT_EXIST = -30017,
  kErrCode_ENTRY_NOT_FOUND = -30018,
  kErrCode_ACCEPTOR_QUEUE_FULL = -30019,
  kErrCode_REMOTE_NEED_CATCHUP = -30020,
  kErrCode_WORKING_IN_PROPGRESS = -30021,
  kErrCode_PROPOSER_QUEUE_FULL = -30022,
  kErrCode_SETUP_TIMEOUT_FAIL = -30023,
  kErrCode_PEER_ACCEPT_FAIL = -30024,    // prepare return peer's value
  kErrCode_INVALID_ACCEPT_RSP = -30025,  // prepare return peer's value
  kErrCode_PEER_ACCEPT_REJECTED = -30026,
  kErrCode_PEER_ACCEPT_OTHER_VALUE = -30027,
  kErrCode_PREPARE_REJECTED = -30008,  // prepare return peer's value
  kErrCode_LAST_INTERNAL_ERR = -100000,
};

constexpr int MAX_ACCEPTOR_NUM = 7;

struct Proposal {
  uint64_t pid_;   // proposal id ,pid == 0 indicates a read probe
  uint64_t term_;  // logical time

  uint64_t pinst_;       // plog id
  uint64_t pentry_;      // plog entry
  uint64_t last_chosen_;       // last chosen entry from source
  uint16_t last_chosen_from_;  // proposer for last chosen entry
  uint16_t proposer_;

  uint32_t size_;  // sizeof value
  uint32_t status_;
  uint64_t opaque_;    // opaque data for value used by upper application
  uint64_t value_id_;  // an unique id for every proposed value.

  uint8_t data_[1];  // struct hack
} __attribute__((packed, aligned(1)));

struct PaxosMsg {
  uint32_t magic_;
  uint32_t size_;
  uint32_t type_;
  uint32_t version_;
  uint32_t from_;  // svr id
  int32_t errcode_;
  uint64_t reqid_;   // rpc id
  uint8_t data_[1];  // struct hack
} __attribute__((packed, aligned(1)));

constexpr auto PaxosMsgHeaderSz = offsetof(PaxosMsg, data_);
constexpr auto ProposalHeaderSz = offsetof(Proposal, data_);

int CopyProposalMeta(Proposal& dst, const Proposal& src);
std::shared_ptr<Proposal> CloneProposal(const Proposal& pm);
std::shared_ptr<Proposal> AllocProposal(uint32_t value_size);
std::shared_ptr<PaxosMsg> CloneProposalMsg(const PaxosMsg& pm);
std::shared_ptr<PaxosMsg> AllocProposalMsg(uint32_t value_size);

inline Proposal GenDummyProposal() {
  Proposal d;
  memset(&d, 0, sizeof(d));
  d.size_ = 1;
  return d;
}

inline Proposal* GetProposalFromMsg(PaxosMsg* pm) {
  return reinterpret_cast<Proposal*>(pm->data_);
}

inline uint64_t GetPLIdFromMsg(const PaxosMsg* pm) {
  return reinterpret_cast<const Proposal*>(pm->data_)->pinst_;
}
}  // namespace quarrel

#endif
