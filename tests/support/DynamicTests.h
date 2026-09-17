#pragma once

// Test suites that are generated at runtime (one per registered engine adapter, one per
// replay fixture). Called from tests/main.cpp before RUN_ALL_TESTS().
namespace lankey::tests {

void registerConformanceTests();
void registerReplayTests();

} // namespace lankey::tests
