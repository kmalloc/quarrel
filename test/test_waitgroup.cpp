#include "gtest/gtest.h"
#include <string>
#include <thread>
#include <chrono>

#include "waitgroup.hpp"

using namespace quarrel;

TEST(waitgroup, testwg) {
    WaitGroup wg(3);

    std::thread t1([&]() { wg.Notify(); });
    std::thread t2([&]() { wg.Notify(); });

    ASSERT_FALSE(wg.Wait(3));

    std::thread t3([&]() { wg.Notify(); });

    ASSERT_TRUE(wg.Wait(3));
    ASSERT_TRUE(wg.Wait(3));

    std::thread t4([&]() { wg.Notify(); });

    auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(wg.Wait(3));
    auto end = std::chrono::steady_clock::now();

    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    ASSERT_LT(diff, 3);

    t1.join();
    t2.join();
    t3.join();
    t4.join();
}

TEST(waitgroup, testwgtimeout) {
    WaitGroup wg(3);
    auto tm = std::chrono::milliseconds(10);

    std::thread t1([&]() { wg.Notify(); });
    std::thread t2([&]() { wg.Notify(); });
    std::thread t3([&]() { std::this_thread::sleep_for(tm); wg.Notify(); });

    ASSERT_FALSE(wg.Wait(8));

    ASSERT_TRUE(wg.Wait(3));

    t1.join();
    t2.join();
    t3.join();
}
