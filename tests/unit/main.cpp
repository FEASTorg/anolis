#include <gmock/gmock.h>
#include <gtest/gtest.h>

int main(int argc, char **argv) {
    // Explicitly initialize GoogleTest so --gtest_list_tests and filters work
    // reliably during CTest discovery.
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
