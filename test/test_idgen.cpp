#include "gtest/gtest.h"

#include "idgen.hpp"

using namespace quarrel;

TEST(idgen, idgenapi) {
    IdGen ig(1, 5);

    ASSERT_EQ(5, ig.GetStep());
    ASSERT_EQ(6, ig.GetAndInc());
    ASSERT_EQ(11, ig.GetAndInc());

    ig.SetID(33);
    ASSERT_EQ(38, ig.GetAndInc());
}
