#ifndef __QUARREL_PTYPE_H_
#define __QUARREL_PTYPE_H_

#include <memory>
#include <string>
#include <cstdint>

namespace quarrel {

    enum {
        kPaxosState_INIT = 0,
        kPaxosState_PREPARE = 1,
        kPaxosState_PROMISE = 2,
        kPaxosState_ACCEPTED = 3,
        kPaxosState_COMMITED = 4,
    };

    enum {
        kPaxosMsgType_INVALID = 0,
        kPaxosMsgType_PREPARE_REQ = 1,
        kPaxosMsgType_PREPARE_RSP = 2,
        kPaxosMsgType_ACCEPT_REQ = 3,
        kPaxosMsgType_ACCEPT_RSP = 4,
        kPaxosMsgType_CHORE = 100,
    };

    constexpr int MAX_ACCEPTOR_NUM = 32;

    struct ProposalValue {
        std::string value_;
        uintptr_t value_opaque_; // opaque data for value used by upper application
    };

    struct Proposal {
        uint64_t pid_; // proposal id
        ProposalValue value_;

        uint64_t plid_; // plog id
        uint64_t pentry_; // plog entry

        int proposer_;
    };

    struct PaxosMsg {
        uint32_t magic_;
        uint32_t size_;
        uint32_t type_;
        uint32_t sender_;
        char data_[0];
    };

    struct PaxosStateMachine {
        uint32_t state_;
        uint64_t prepare_id_;
        uint64_t accepted_id_;

        Proposal proposal_;

        ProposalValue value_from_peer_; // value from peer
        uint32_t accepted_id_from_peer_;

        uint32_t num_promise_; // number of acceptor who sent promise
        uint32_t num_accepted_; // number of acceptor who accepted.
        uint64_t acceptor_promised_[MAX_ACCEPTOR_NUM]; // acceptors who gave promise
        uint64_t acceptor_accepted_[MAX_ACCEPTOR_NUM];
    };
}

#endif
