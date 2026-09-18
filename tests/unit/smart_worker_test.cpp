#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "core/smart/SmartWorker.h"
#include "core/threading/SpscQueue.h"
#include "core/threading/TaskThread.h"

#include "tests/fakes/FakeClock.h"
#include "tests/fakes/InMemoryLexiconStore.h"

namespace lankey::core {
namespace {

using model::Settings;
using model::Syllable;
using model::SyllableCommitted;
using tests::FakeClock;
using tests::InMemoryLexiconStore;

TEST(SpscQueue, PushPopOrderAndFull) {
    threading::SpscQueue<int, 4> q; // holds Capacity-1 = 3 items
    EXPECT_TRUE(q.empty());
    EXPECT_TRUE(q.tryPush(1));
    EXPECT_TRUE(q.tryPush(2));
    EXPECT_TRUE(q.tryPush(3));
    EXPECT_FALSE(q.tryPush(4)); // full: dropped, never blocks
    EXPECT_EQ(*q.tryPop(), 1);
    EXPECT_TRUE(q.tryPush(4));
    EXPECT_EQ(*q.tryPop(), 2);
    EXPECT_EQ(*q.tryPop(), 3);
    EXPECT_EQ(*q.tryPop(), 4);
    EXPECT_FALSE(q.tryPop().has_value());
}

TEST(SpscQueue, ProducerConsumerThreadsSeeEveryItem) {
    threading::SpscQueue<int, 1024> q;
    constexpr int kCount = 100000;
    std::atomic<long long> sum{0};
    std::thread consumer([&] {
        int seen = 0;
        while (seen < kCount) {
            if (auto v = q.tryPop()) {
                sum += *v;
                ++seen;
            } else {
                std::this_thread::yield();
            }
        }
    });
    for (int i = 1; i <= kCount;) {
        if (q.tryPush(i)) ++i;
    }
    consumer.join();
    EXPECT_EQ(sum.load(), static_cast<long long>(kCount) * (kCount + 1) / 2);
}

TEST(TaskThread, RunsTasksInOrderAndDrains) {
    threading::TaskThread t("test");
    t.start();
    std::string order;
    std::mutex m;
    for (const char c : {'a', 'b', 'c'}) {
        t.post([&, c] {
            std::lock_guard lock(m);
            order.push_back(c);
        });
    }
    t.drain();
    EXPECT_EQ(order, "abc");
    t.post([] { throw std::runtime_error("boom"); }); // must not kill the thread
    t.post([&] {
        std::lock_guard lock(m);
        order.push_back('d');
    });
    t.drain();
    EXPECT_EQ(order, "abcd");
    t.stop();
    t.stop(); // idempotent
}

struct WorkerTest : testing::Test {
    InMemoryLexiconStore store;
    smart::SuggestionEngine suggestions;
    smart::AutoCorrectEngine corrector;
    FakeClock clock;
    Settings settings;
    smart::SmartWorker worker{{store, suggestions, corrector, clock}, settings};

    static SyllableCommitted event(std::initializer_list<const char32_t*> syllables,
                                   std::string app = "notepad.exe") {
        SyllableCommitted c;
        for (const auto* s : syllables)
            c.window.commit(Syllable::fromComposed(s));
        c.focus.appName = std::move(app);
        return c;
    }
};

TEST_F(WorkerTest, LearnsCommitsFlushesOnStopAndRebuildsSnapshot) {
    worker.start();
    for (int i = 0; i < 3; ++i)
        worker.onCommit(event({U"chương", U"trình"}));
    worker.stop(); // flushes pending learning through the DB thread

    const auto* e = store.find(U"chương trình");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->frequency, 3u);
    EXPECT_EQ(store.find(U"trình")->frequency, 3u);
    EXPECT_EQ(worker.stats().eventsProcessed.load(), 3u);
    EXPECT_EQ(worker.stats().flushes.load(), 1u);

    // A fresh worker over the same store makes the phrase suggestable after its first load.
    smart::SuggestionEngine engine2;
    smart::AutoCorrectEngine corrector2;
    smart::SmartWorker w2({store, engine2, corrector2, clock}, settings);
    w2.start();
    w2.drainForTests();
    EXPECT_TRUE(engine2.hasSnapshot());
    model::SuggestionQuery q;
    q.typed = U"chư";
    q.prefix = U"chư";
    const auto list = engine2.suggest(q);
    ASSERT_FALSE(list.empty());
    EXPECT_EQ(list[0].phrase.joined(), U"chương trình");
    w2.stop();
}

TEST_F(WorkerTest, PrivacyRejectsNeverReachTheStore) {
    worker.start();
    worker.onCommit(event({U"chương"}, "keepass.exe"));
    worker.onCommit(event({U"P@ss1"}));
    SyllableCommitted pw = event({U"chương"});
    pw.focus.isPasswordField = true;
    worker.onCommit(std::move(pw));
    worker.stop();
    EXPECT_EQ(store.size(), 0u);
    EXPECT_EQ(worker.stats().rejectedByPrivacy.load(), 3u);
}

TEST_F(WorkerTest, SelectionCountsWithHigherWeight) {
    worker.start();
    model::Phrase p;
    p.syllables = {Syllable::fromComposed(U"hệ"), Syllable::fromComposed(U"điều")};
    worker.onSelection(p);
    worker.stop();
    ASSERT_NE(store.find(U"hệ điều"), nullptr);
    EXPECT_EQ(store.find(U"hệ điều")->frequency,
              static_cast<std::uint32_t>(smart::LearningRecorder::kSelectionWeight));
}

TEST_F(WorkerTest, EraseAllDataClearsStoreAndSnapshot) {
    ASSERT_TRUE(store.applyDeltas({{{{Syllable::fromComposed(U"abc")}}, 5, 1}}).has_value());
    worker.start();
    worker.drainForTests();
    EXPECT_TRUE(suggestions.hasSnapshot());
    bool called = false;
    worker.eraseAllData([&](lk::expected<void> r) { called = r.has_value(); });
    worker.drainForTests();
    EXPECT_TRUE(called);
    EXPECT_EQ(store.size(), 0u);
    model::SuggestionQuery q;
    q.typed = U"ab";
    q.prefix = U"ab";
    EXPECT_TRUE(suggestions.suggest(q).empty());
    worker.stop();
}

TEST_F(WorkerTest, SettingsUpdateRebuildsPrivacyFilter) {
    worker.start();
    Settings s;
    s.privacy.excludedApps.push_back("mybank.exe");
    worker.updateSettings(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the worker pick it up
    worker.onCommit(event({U"chương"}, "mybank.exe"));
    worker.onCommit(event({U"chương"}, "notepad.exe"));
    worker.stop();
    EXPECT_EQ(worker.stats().rejectedByPrivacy.load(), 1u);
    EXPECT_EQ(store.size(), 1u);
}

TEST_F(WorkerTest, ProposesCorrectionsAndLearnsTheCorrectedPhrase) {
    // The user writes "đường" a lot; "đưởng" (wrong tone key) gets corrected.
    ASSERT_TRUE(store.applyDeltas({{{{Syllable::fromComposed(U"đường")}}, 5, 1}}).has_value());
    std::vector<model::Correction> proposed;
    std::mutex m;
    worker.setCorrectionHandler([&](model::Correction c) {
        const std::lock_guard<std::mutex> lock(m);
        proposed.push_back(std::move(c));
    });
    worker.start();
    worker.drainForTests(); // base index + first snapshot are ready

    auto e = event({U"đưởng"});
    e.terminator = U' ';
    e.vietnameseTransformApplied = true;
    e.generation = 7;
    worker.onCommit(std::move(e));
    worker.drainForTests();
    {
        const std::lock_guard<std::mutex> lock(m);
        ASSERT_EQ(proposed.size(), 1u);
        EXPECT_EQ(proposed[0].corrected[0].text, U"đường");
        EXPECT_EQ(proposed[0].expectedGeneration, 7u);
    }
    worker.stop();
    // What was learned is the corrected word, not the typo.
    EXPECT_EQ(store.find(U"đường")->frequency, 6u);
    EXPECT_EQ(store.find(U"đưởng"), nullptr);
    EXPECT_EQ(worker.stats().correctionsProposed.load(), 1u);
}

TEST_F(WorkerTest, ManualFixesAndRejectionsReachTheStore) {
    worker.start();
    worker.drainForTests();

    auto e = event({U"sửa", U"lỗi"});
    e.retypedFrom = {Syllable::fromComposed(U"sữa"), Syllable::fromComposed(U"lỗi")};
    worker.onCommit(std::move(e));
    worker.drainForTests();
    const auto* rule = store.rule(U"sữa lỗi");
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->correct, U"sửa lỗi");
    EXPECT_NEAR(rule->confidence, model::Thresholds::kCorrectionLearnStep, 1e-9);
    EXPECT_EQ(worker.stats().manualFixesLearned.load(), 1u);

    worker.onCorrectionRejected(U"sữa lỗi", U"sửa lỗi");
    worker.onCorrectionRejected(U"sữa lỗi", U"sửa lỗi");
    worker.drainForTests();
    worker.stop();
    EXPECT_EQ(store.rule(U"sữa lỗi")->timesRejected, 2);
    EXPECT_EQ(store.loadBlacklist()->size(), 1u);
    EXPECT_EQ(worker.stats().correctionsRejected.load(), 2u);
    // What the user typed was learned back; what we put in was taken back (never below 0).
    ASSERT_NE(store.find(U"sữa lỗi"), nullptr);
    EXPECT_EQ(store.find(U"sữa lỗi")->frequency, 2u);
    EXPECT_EQ(store.find(U"sửa lỗi")->frequency, 0u);
}

} // namespace
} // namespace lankey::core
