#include "gtest/gtest.h"

#include "plog.h"

using namespace quarrel;

TEST(quarrel_plog, test_entry_serialization) {
    Configure config = {0};
    config.local_id_ = 23;
    config.total_acceptor_ = 23;

    Entry ent(config, 3, 122);

    auto p1 = AllocProposal(233);
    auto p2 = AllocProposal(133);

    p1->pid_ = 444;
    p1->term_ = 555;
    p1->plid_ = 3;
    p1->pentry_ = 122;
    p1->proposer_ = 1;
    p1->batch_num_ = 1;
    p1->opaque_ = 0xbadf00d;
    p1->value_id_ = 0xbadf11d;
    p1->status_ = kPaxosState_PROMISED;
    strcpy(reinterpret_cast<char*>(p1->data_), "dummy data for p1");

    p2->pid_ = 333;
    p2->term_ = 666;
    p2->plid_ = 3;
    p2->pentry_ = 122;
    p2->proposer_ = 2;
    p2->batch_num_ = 1;
    p2->opaque_ = 0xbadf22d;
    p2->value_id_ = 0xbadf33d;
    p2->status_ = kPaxosState_ACCEPTED;
    strcpy(reinterpret_cast<char*>(p2->data_), "dummy data for p2");

    ent.SetProposal(p1);
    ent.SetPromise(p2);

    std::string to;

    Entry ent2(config, 44, 111);
    ASSERT_EQ(ent.SerializeTo(to), p1->size_ + p2->size_ + sizeof(EntryRaw) - 1 + 2 * ProposalHeaderSz);
    ASSERT_EQ(kErrCode_OK, ent2.UnserializeFrom(to));

    ASSERT_EQ(ent.GenValueId(), ent2.GenValueId());
    ASSERT_EQ(ent.GenPrepareId(), ent2.GenPrepareId());

    const auto& pp1 = ent2.GetProposal();
    const auto& pp2 = ent2.GetPromised();

    ASSERT_EQ(0, memcmp(p1.get(), pp1.get(), ProposalHeaderSz+p1->size_));
    ASSERT_EQ(0, memcmp(p2.get(), pp2.get(), ProposalHeaderSz+p2->size_));
}
