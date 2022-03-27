#include "gtest/gtest.h"

#include "base/fix_count_allocator.hpp"

namespace base {

TEST(fix_count_alloc, vector_queue) {
  int alloc_count = 0, release_count = 0;
  auto alloc_int = [&]() { alloc_count++; return new int; };
  auto release_int = [&](int* p) { release_count++; delete p; };

  FixCountVectorAllocator<int*> q1;

  q1.Init(2, 3, alloc_int, release_int);

  std::vector<int*> r1[2], r2[2];

  int alloc_times = 7;
  for (int i = 0; i < alloc_times; i++) {
    auto d = q1.AllocByShard(i);
    ASSERT_NE(NULL, d);
    r1[i % 2].push_back(d);
  }

  ASSERT_EQ(alloc_times, alloc_count);
  ASSERT_EQ(alloc_times / 2 + 1, r1[0].size());
  ASSERT_EQ(alloc_times / 2, r1[1].size());

  for (auto i = 0u; i < r1[0].size(); i++) {
    q1.ReleaseByShard(0, r1[0][i]);
  }

  for (auto i = 0u; i < r1[1].size(); i++) {
    q1.ReleaseByShard(1, r1[1][i]);
  }

  ASSERT_EQ(1, release_count);

  for (int i = 0u; i < alloc_times; i++) {
    auto d = q1.AllocByShard(i);
    ASSERT_NE(NULL, d);
    r2[i % 2].push_back(d);
  }

  ASSERT_EQ(alloc_times + 1, alloc_count);

  for (int i = 0u; i < alloc_times / 2; i++) {
    std::cout << i << ", r1[0]:" << r1[0][i] << ", r2[0]:" << r2[0][i] << std::endl;
  }
  for (int i = 0u; i < alloc_times / 2; i++) {
    std::cout << i << ", r1[1]:" << r1[1][i] << ", r2[1]:" << r2[1][i] << std::endl;
  }

  for (int i = 0u; i < alloc_times / 2; i++) {
    ASSERT_EQ(r1[0][i], r2[0][alloc_times / 2 - 1 - i]) << "th:" << i;
  }
  for (int i = 0u; i < alloc_times / 2; i++) {
    ASSERT_EQ(r1[1][i], r2[1][alloc_times / 2 - 1 - i]) << "th:" << i;
  }

  for (auto i = 0u; i < r2[0].size(); i++) {
    q1.ReleaseByShard(0, r2[0][i]);
  }

  for (auto i = 0u; i < r2[1].size(); i++) {
    q1.ReleaseByShard(1, r2[1][i]);
  }

  r1[0].clear();
  r1[1].clear();
}

TEST(fix_count_alloc, lockfree_queue) {
  int alloc_count = 0, release_count = 0;
  auto alloc_int = [&]() { alloc_count++; return new int; };
  auto release_int = [&](int* p) { release_count++; delete p; };

  FixCountLockFreeAllocator<int*> q1;

  q1.Init(2, 3, alloc_int, release_int);

  std::vector<int*> r1[2], r2[2];

  int alloc_times = 7;
  for (int i = 0; i < alloc_times; i++) {
    auto d = q1.AllocByShard(i);
    ASSERT_NE(NULL, d);
    r1[i % 2].push_back(d);
  }

  ASSERT_EQ(alloc_times, alloc_count);
  ASSERT_EQ(alloc_times / 2 + 1, r1[0].size());
  ASSERT_EQ(alloc_times / 2, r1[1].size());

  for (auto i = 0u; i < r1[0].size(); i++) {
    q1.ReleaseByShard(0, r1[0][i]);
  }

  for (auto i = 0u; i < r1[1].size(); i++) {
    q1.ReleaseByShard(1, r1[1][i]);
  }

  ASSERT_EQ(1, release_count);

  for (int i = 0u; i < alloc_times; i++) {
    auto d = q1.AllocByShard(i);
    ASSERT_NE(NULL, d);
    r2[i % 2].push_back(d);
  }

  ASSERT_EQ(alloc_times + 1, alloc_count);

  for (int i = 0u; i < alloc_times / 2; i++) {
    std::cout << i << ", r1[0]:" << r1[0][i] << ", r2[0]:" << r2[0][i] << std::endl;
  }
  for (int i = 0u; i < alloc_times / 2; i++) {
    std::cout << i << ", r1[1]:" << r1[1][i] << ", r2[1]:" << r2[1][i] << std::endl;
  }

  for (int i = 0u; i < alloc_times / 2; i++) {
    ASSERT_EQ(r1[0][i], r2[0][i]) << "th:" << i;
  }
  for (int i = 0u; i < alloc_times / 2; i++) {
    ASSERT_EQ(r1[1][i], r2[1][i]) << "th:" << i;
  }

  for (auto i = 0u; i < r2[0].size(); i++) {
    q1.ReleaseByShard(0, r2[0][i]);
  }

  for (auto i = 0u; i < r2[1].size(); i++) {
    q1.ReleaseByShard(1, r2[1][i]);
  }

  r1[0].clear();
  r1[1].clear();
}

}  // namespace base
