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
        kPaxosState_PREPARE = 1,
        kPaxosState_PROMISE = 2,
        kPaxosState_ACCEPTED = 3,
        kPaxosState_COMMITED = 4,
    };

    enum PaxosMsgType {
        kPaxosMsgType_INVALID = 0,
        kPaxosMsgType_PREPARE_REQ = 1,
        kPaxosMsgType_PREPARE_RSP = 2,
        kPaxosMsgType_ACCEPT_REQ = 3,
        kPaxosMsgType_ACCEPT_RSP = 4,
        kPaxosMsgType_CHORE = 100,
    };

    enum PaxosErrCode {
        kPaxosErrCode_INVALID = 0,
        kPaxosErrCode_OOM = 3001,
        kPaxosErrCode_TIMEOUT= 3002,
    };

    constexpr int MAX_ACCEPTOR_NUM = 32;

    struct Proposal {
        uint64_t pid_; // proposal id
        uint64_t term_; // logical time

        uint64_t plid_; // plog id
        uint64_t pentry_; // plog entry
        uint16_t proposer_;

        uint32_t size_; // sizeof value
        uint64_t opaque_; // opaque data for value used by upper application

        uint8_t data_[1]; // google struct hack
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

    struct PaxosMsgDeleter {
        void operator()(void* x) { free(x); }
    };

    using PaxosMsgPtr = std::unique_ptr<PaxosMsg, PaxosMsgDeleter>;
    constexpr auto PaxosMsgHeaderSz = offsetof(PaxosMsg, data_);
    constexpr auto ProposalHeaderSz = offsetof(Proposal, data_);

    PaxosMsgPtr AllocProposalMsg(uint32_t value_size);
}

#endif
