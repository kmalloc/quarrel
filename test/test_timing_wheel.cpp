#include "gtest/gtest.h"
#include <string>
#include <thread>
#include <chrono>

#include "waitgroup.hpp"
#include "timing_wheel.hpp"

using namespace quarrel;

TEST(tm_all, test_timing_wheel) {
  TimingWheel tm(20);
  tm.Start();

  WaitGroup wg(1);

  auto now = std::chrono::steady_clock::now();
  auto compare = [&](uint64_t opaque, uint64_t expect, int expectms) {
    auto end = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();

    ASSERT_EQ(opaque, expect);
    ASSERT_GE(diff, expectms);
  };

  tm.AddTimeout(111, 5, [&](uint64_t opaque) { compare(opaque, 111, 20); wg.Notify(); });
  wg.Wait(1000);
  wg.Reset(1);

  now = std::chrono::steady_clock::now();
  tm.AddTimeout(222, 40, [&](uint64_t opaque) { compare(opaque, 222, 40); wg.Notify(); });
  wg.Wait(1000);
  wg.Reset(1);

  now = std::chrono::steady_clock::now();
  tm.AddTimeout(333, 50, [&](uint64_t opaque) { compare(opaque, 333, 60); wg.Notify(); });
  wg.Wait(1000);
  wg.Reset(1);

  now = std::chrono::steady_clock::now();
  tm.AddTimeout(333, 150, [&](uint64_t opaque) { compare(opaque, 333, 160); wg.Notify(); });
  wg.Wait(1000);
  wg.Reset(1);
}
