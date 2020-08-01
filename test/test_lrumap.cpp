#include "gtest/gtest.h"

#include <stdexcept>

#include "lrumap.hpp"

using namespace quarrel;

TEST(lrumap, testapi) {
    LruMap<int, int> m(3);

    m.Put(1, 100);
    m.Put(2, 102);
    m.Put(3, 103);

    ASSERT_EQ(100, m.Get(1));
    ASSERT_EQ(102, m.Get(2));
    ASSERT_EQ(103, m.Get(3));

    ASSERT_TRUE(m.Exists(1));
    ASSERT_TRUE(m.Exists(2));
    ASSERT_TRUE(m.Exists(3));
    ASSERT_FALSE(m.Exists(4));

    ASSERT_THROW(m.Get(4), std::range_error);

    m.Put(4, 200);
    ASSERT_TRUE(m.Exists(4));
    ASSERT_FALSE(m.Exists(1));

    m.Put(5, 300);
    ASSERT_TRUE(m.Exists(5));
    ASSERT_FALSE(m.Exists(2));

    ASSERT_EQ(3, m.Size());

    m.Get(3);
    m.Put(6, 400);
    ASSERT_TRUE(m.Exists(6));
    ASSERT_TRUE(m.Exists(3));
    ASSERT_FALSE(m.Exists(4));

    ASSERT_TRUE(m.Del(3));
    ASSERT_FALSE(m.Del(3));
    ASSERT_FALSE(m.Exists(3));
    ASSERT_THROW(m.Get(3), std::range_error);

    ASSERT_EQ(2, m.Size());
}
