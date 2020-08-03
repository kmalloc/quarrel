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
        kPaxosState_COMMITED = 5,
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
    };

    enum PaxosErrCode {
        kErrCode_OK = 0,
        kErrCode_OOM = 3001,
        kErrCode_TIMEOUT= 3002,
        kErrCode_NOT_QUORAUM = 3003,
        kErrCode_CONN_FAIL = 3004,
    };

    constexpr int MAX_ACCEPTOR_NUM = 32;

    struct Proposal {
        uint64_t pid_; // proposal id
        uint64_t term_; // logical time

        uint64_t plid_; // plog id
        uint64_t pentry_; // plog entry
        uint16_t proposer_;

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
        uint32_t to_;
        uint32_t from_;
        uint64_t reqid_; // rpc id
        uint8_t  data_[1]; // struct hack
    } __attribute__((packed, aligned(1)));

    struct PaxosStateMachine {
        uint32_t state_;
        uint64_t prepare_id_;
        uint64_t accepted_id_;

        uint32_t accepted_id_from_peer_;
        std::unique_ptr<Proposal> proposal_;

        uint32_t num_promise_; // number of acceptor who sent promise
        uint32_t num_accepted_; // number of acceptor who accepted.
        uint64_t acceptor_promised_[MAX_ACCEPTOR_NUM]; // acceptors who gave promise
        uint64_t acceptor_accepted_[MAX_ACCEPTOR_NUM]; // acceptors who accepted
    };

    constexpr auto PaxosMsgHeaderSz = offsetof(PaxosMsg, data_);
    constexpr auto ProposalHeaderSz = offsetof(Proposal, data_);

    std::shared_ptr<PaxosMsg> AllocProposalMsg(uint32_t value_size);
}

#endif
