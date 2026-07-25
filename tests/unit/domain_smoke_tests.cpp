#include "backbook/domain/domain.hpp"

#include <gtest/gtest.h>

TEST(DomainSmokeTest, LibraryIsAvailable) {
    EXPECT_TRUE(backbook::domain::is_available());
}
