#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace lankey::core::threading {

// A serial executor: one thread running posted tasks in order. Used for the DB thread,
// where every operation must be sequential and none may touch the hook thread.
//
// post() may be called from any thread except the hook thread (it takes a mutex).
class TaskThread {
public:
    using Task = std::function<void()>;

    explicit TaskThread(std::string name) : name_(std::move(name)) {}
    ~TaskThread() { stop(); }

    TaskThread(const TaskThread&) = delete;
    TaskThread& operator=(const TaskThread&) = delete;

    void start() {
        std::lock_guard lock(mutex_);
        if (thread_.joinable()) return;
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    // Runs every task already posted, then joins. Idempotent.
    void stop() {
        {
            std::lock_guard lock(mutex_);
            if (!running_) return;
            running_ = false;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void post(Task task) {
        {
            std::lock_guard lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        cv_.notify_one();
    }

    // Blocks until every task posted before this call has run. Test helper / shutdown aid.
    void drain() {
        std::mutex m;
        std::condition_variable done;
        bool finished = false;
        post([&] {
            std::lock_guard lock(m);
            finished = true;
            done.notify_all();
        });
        std::unique_lock lock(m);
        done.wait(lock, [&] { return finished; });
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    void run() {
        for (;;) {
            Task task;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return !running_ || !tasks_.empty(); });
                if (tasks_.empty()) {
                    if (!running_) return;
                    continue;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            try {
                task();
            } catch (...) {
                // A failing task must not take the thread down with it; the caller that
                // posted it reports its own errors through lk::expected.
            }
        }
    }

    std::string name_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    std::thread thread_;
    bool running_ = false;
};

} // namespace lankey::core::threading
