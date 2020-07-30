#ifndef __QUARREL_ACCEPTOR_H_
#define __QUARREL_ACCEPTOR_H_

#include "ptype.h"
#include "queue.h"
#include "entry.h"

#include <functional>

namespace quarrel {
    using AcceptorNotifier = int(PaxosState, const Proposal&);

    struct ProposalEvent {
        AcceptorNotifier notifier_;
        std::unique_ptr<Proposal> proposal_;
    };

    class Acceptor {
        public:
            Acceptor(int id, bool is_local);
            ~Acceptor();

            int StartWorker();
            int AddEvent(AcceptorNotifier notify, std::unique_ptr<Proposal> ev);

        private:
            Acceptor(const Acceptor&) = delete;
            Acceptor& operator=(const Acceptor&) = delete;

            int HandleEvent(std::unique_ptr<Proposal> p);

        private:
            bool is_local_;

            uint64_t local_chosen_entry_;
            uint64_t global_chosen_entry_;

            PaxosStateMachine state_;
            LockFreeQueue<ProposalEvent> event_;
    };
};

#endif
