#include "gtest/gtest.h"

#include <string>
#include <thread>
#include <chrono>
#include <future>

#include "logger.h"

#include "acceptor.h"

using namespace quarrel;

struct DummyEntryMng: public EntryMng {
    DummyEntryMng(std::shared_ptr<Configure> config, uint64_t pinst)
        :EntryMng(std::move(config), pinst) {}

    virtual int SaveEntry(uint64_t pinst, uint64_t entry, const Entry& ent) {
        (void)pinst;(void)entry;(void)ent;
        return kErrCode_OK;
    }
    virtual int LoadEntry(uint64_t pinst, uint64_t entry, Entry& ent) {
        (void)pinst;(void)entry;(void)ent;
        return kErrCode_OK;
    }
    virtual int Checkpoint(uint64_t pinst, uint64_t term) {
        (void)pinst;(void)term;
        return kErrCode_OK;
    }
    virtual uint64_t GetMaxCommittedEntry(uint64_t pinst) {
        (void)pinst;
        return max_committed_++%8;
    }
    virtual int LoadUncommittedEntry(std::vector<std::unique_ptr<Entry>>& entries) {
        (void)entries;
        return kErrCode_OK;
    }

    uint64_t max_committed_{0};
};

TEST(acceptor_test, test_acceptor_api) {
    auto config = std::make_shared<Configure>();
    config->timeout_ = 8; // 8ms
    config->local_ = {1, ConnType_LOCAL, "xxxx:yyy"};
    config->local_id_ = 1;
    config->plog_inst_num_ = 5;
    config->total_acceptor_ = 3;
    config->peer_.push_back({2, ConnType_REMOTE, "aaaa:bb"});
    config->peer_.push_back({3, ConnType_REMOTE, "aaaa2:bb2"});
    std::shared_ptr<PlogMng> pmn = std::make_shared<PlogMng>(config);

    auto entry_mng_creator = [](int pinst, std::shared_ptr<Configure> conf) -> std::unique_ptr<EntryMng> {
        return std::unique_ptr<EntryMng>(new DummyEntryMng(std::move(conf), pinst));
    };

    pmn->SetEntryMngCreator(entry_mng_creator);
    pmn->InitPlog();
}
