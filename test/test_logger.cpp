#include "gtest/gtest.h"
#include <string>

#include "logger.h"

using namespace quarrel;

TEST(logger, testlog) {
    int op = 0;
    std::string log;
    auto flush = [&](){ op++; };
    auto output = [&](const char* m, int sz) { log.append(m, sz); };

    Logger::setFlush(flush);
    Logger::setOutput(output);

    LOG_WARN << "hello";
    ASSERT_EQ(0, op);
    ASSERT_TRUE(log.find("hello") != std::string::npos);
    ASSERT_TRUE(log.find("WARN") != std::string::npos);

    LOG_INFO << "world";
    ASSERT_EQ(0, op);
    ASSERT_TRUE(log.find("world") != std::string::npos);
    ASSERT_TRUE(log.find("INFO") != std::string::npos);
}
