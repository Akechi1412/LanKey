// Replay tests: every directory under tests/replay/fixtures/ becomes one test that runs the
// recorded keys through the real engine + InputPipeline and compares the outcome with
// expected.txt (and commits.txt when present).
//
// Each real-world bug found during dogfooding becomes a fixture here BEFORE it is fixed.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "tests/replay/ReplayHarness.h"
#include "tests/support/DynamicTests.h"
#include "tests/unit/engine_registry.h"

#ifndef LANKEY_REPLAY_FIXTURES_DIR
#error "LANKEY_REPLAY_FIXTURES_DIR must be defined by CMake"
#endif

namespace lankey::tests {
namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& p) {
    const std::ifstream in(p, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    std::string s = buf.str();
    // Normalise line endings and ignore one trailing newline so editors do not break tests.
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line))
        out.push_back(line);
    return out;
}

class ReplayFixtureTest : public testing::Test {
public:
    ReplayFixtureTest(EngineCandidate engine, fs::path dir)
        : engine_(std::move(engine)), dir_(std::move(dir)) {}

    void TestBody() override {
        auto engine = engine_.make();
        ASSERT_NE(engine, nullptr);
        const auto result = ReplayHarness::run(dir_, *engine);
        ASSERT_TRUE(result.has_value()) << result.error().message;

        const std::string expectedScreen = readFile(dir_ / "expected.txt");
        EXPECT_EQ(ReplayHarness::toUtf8(result->screen), expectedScreen);

        if (fs::exists(dir_ / "commits.txt")) {
            const auto expected = lines(readFile(dir_ / "commits.txt"));
            EXPECT_EQ(result->commits, expected);
        }
    }

private:
    EngineCandidate engine_;
    fs::path dir_;
};

} // namespace

void registerReplayTests() {
    const fs::path root{LANKEY_REPLAY_FIXTURES_DIR};
    const auto engines = registeredEngines();
    if (engines.empty() || !fs::is_directory(root)) return;

    std::vector<fs::path> dirs;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_directory() && fs::exists(entry.path() / "keys.keylog")) {
            dirs.push_back(entry.path());
        }
    }
    std::ranges::sort(dirs);

    for (const auto& engine : engines) {
        const std::string suite = "Replay_" + engine.name;
        for (const auto& dir : dirs) {
            const std::string name = dir.filename().string();
            testing::RegisterTest(suite.c_str(), name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
                                  [engine, dir] { return new ReplayFixtureTest(engine, dir); });
        }
    }
}

} // namespace lankey::tests
