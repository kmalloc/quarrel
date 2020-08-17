#ifndef __QUARREL_PTYPE_H_
#define __QUARREL_PTYPE_H_

#include <memory>
#include <string>
#include <cstddef>
#include <cstdint>
#include <stdlib.h>
#include <functional>

namespace quarrel {

    enum PaxosState {
        kPaxosState_INIT = 0,
        kPaxosState_PREPARED = 1,
        kPaxosState_PROMISED = 2,
        kPaxosState_ACCEPTED = 3,
        kPaxosState_CHOSEN   = 4,
        kPaxosState_ALREADY_CHOSEN = 5,
        kPaxosState_PROMISED_FAILED = 6,
        kPaxosState_ACCEPTED_FAILED = 7,
        kPaxosState_COMMIT_FAILED = 8,
        kPaxosState_INVALID_PROPOSAL = 9,
    };

    enum PaxosMsgType {
        kMsgType_INVALID = 0,
        kMsgType_PREPARE_REQ = 1,
        kMsgType_PREPARE_RSP = 2,
        kMsgType_ACCEPT_REQ = 3,
        kMsgType_ACCEPT_RSP = 4,
        kMsgType_CHOSEN_REQ = 5,
        kMsgType_CHOSEN_RSP = 6,
        kMsgType_INVALID_REQ = 7,
        kMsgType_CHORE_REQ = 101,
    };

    enum PaxosErrCode {
        kErrCode_OK = 0,
        kErrCode_OOM = 3001,
        kErrCode_TIMEOUT= 3002,
        kErrCode_PREPARE_NOT_QUORAUM = 3003,
        kErrCode_ACCEPT_NOT_QUORAUM = 3004,
        kErrCode_CONN_FAIL = 3005,
        kErrCode_PLOG_NOT_EXIST = 3006,
        kErrCode_PREPARE_PEER_VALUE = 3007, // prepare return peer's value
        kErrCode_INVALID_PLOG_DATA = 30008, // invalid formated plog data
        kErrCode_UNMARSHAL_PLOG_FAIL = 30009, // unmarshal plog fail
        kErrCode_WORKER_NOT_STARTED = 30010,
        kErrCode_WORKER_ALREADY_STARTED = 30011,
        kErrCode_PROPOSAL_NOT_EXIST = 30012,
        kErrCode_WRITE_PLOG_FAIL = 30013,
        kErrCode_INVALID_PROPOSAL_REQ = 30014,
    };

    constexpr int MAX_ACCEPTOR_NUM = 32;

    struct Proposal {
        uint64_t pid_; // proposal id
        uint64_t term_; // logical time

        uint64_t plid_; // plog id
        uint64_t pentry_; // plog entry
        uint16_t proposer_;
        uint16_t batch_num_; // for batch preparation, that is, one prepare request for entry slot range from pentry_ to pentry_ + batch_num_;

        uint32_t size_; // sizeof value
        uint32_t status_;
        uint64_t opaque_; // opaque data for value used by upper application
        uint64_t value_id_; // an unique id for every proposed value.

        uint8_t data_[1]; // struct hack
    } __attribute__((packed, aligned(1)));

    struct PaxosMsg {
        uint32_t magic_;
        uint32_t size_;
        uint32_t type_;
        uint32_t version_;
        uint32_t from_;
        uint32_t errcode_;
        uint64_t reqid_; // rpc id
        uint8_t  data_[1]; // struct hack
    } __attribute__((packed, aligned(1)));

    constexpr auto PaxosMsgHeaderSz = offsetof(PaxosMsg, data_);
    constexpr auto ProposalHeaderSz = offsetof(Proposal, data_);

    std::shared_ptr<Proposal> CloneProposal(const Proposal& pm);
    std::shared_ptr<Proposal> AllocProposal(uint32_t value_size);
    std::shared_ptr<PaxosMsg> CloneProposalMsg(const PaxosMsg& pm);
    std::shared_ptr<PaxosMsg> AllocProposalMsg(uint32_t value_size);

    inline Proposal* GetProposalFromMsg(PaxosMsg* pm) {
        return reinterpret_cast<Proposal*>(pm->data_);
    }
}

#endif
