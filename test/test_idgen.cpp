#include "gtest/gtest.h"

#include "idgen.hpp"

using namespace quarrel;

TEST(idgen, idgenapi) {
    IdGen ig(1, 5);

    ASSERT_EQ(5, ig.GetStep());
    ASSERT_EQ(1, ig.GetAndInc());
    ASSERT_EQ(6, ig.GetAndInc());

    ig.SetGreaterThan(33);
    ASSERT_EQ(36, ig.GetAndInc());
}
