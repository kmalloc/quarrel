#include "gtest/gtest.h"

#include <stdexcept>

#include "lrumap.hpp"

using namespace quarrel;

TEST(lrumap, testapi) {
    lru_map<int, int> m(3);

    m.put(1, 100);
    m.put(2, 102);
    m.put(3, 103);

    ASSERT_EQ(100, m.get(1));
    ASSERT_EQ(102, m.get(2));
    ASSERT_EQ(103, m.get(3));

    ASSERT_TRUE(m.exists(1));
    ASSERT_TRUE(m.exists(2));
    ASSERT_TRUE(m.exists(3));
    ASSERT_FALSE(m.exists(4));

    ASSERT_THROW(m.get(4), std::range_error);

    m.put(4, 200);
    ASSERT_TRUE(m.exists(4));
    ASSERT_FALSE(m.exists(1));

    m.put(5, 300);
    ASSERT_TRUE(m.exists(5));
    ASSERT_FALSE(m.exists(2));

    m.get(3);
    m.put(6, 400);
    ASSERT_TRUE(m.exists(6));
    ASSERT_TRUE(m.exists(3));
    ASSERT_FALSE(m.exists(4));
}
