#include <gtest/gtest.h>

#include "tests/support/DynamicTests.h"

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    lankey::tests::registerConformanceTests();
    lankey::tests::registerReplayTests();
    return RUN_ALL_TESTS();
}
