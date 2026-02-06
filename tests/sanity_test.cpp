#include <gtest/gtest.h>

TEST(SanityTest, InfrastructureWorks) {
    EXPECT_TRUE(true);
    EXPECT_EQ(1 + 1, 2);
}

TEST(SanityTest, CanLinkProjectLibraries) {
    // This test just proves we can link against the core libs without symbol errors.
    // We don't need to do anything complex yet.
    EXPECT_TRUE(true);
}
