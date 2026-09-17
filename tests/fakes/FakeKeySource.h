#pragma once

#include <utility>

#include "core/interfaces/IKeySource.h"

namespace lankey::tests {

// Lets a test inject keys exactly as the win32 hook would: press() calls the registered
// handler and returns whether the key was swallowed.
class FakeKeySource final : public core::IKeySource {
public:
    void setHandler(Handler handler) override { handler_ = std::move(handler); }
    void start() override { started_ = true; }
    void stop() override { started_ = false; }

    [[nodiscard]] bool press(const core::model::KeyEvent& key) {
        return started_ && handler_ && handler_(key);
    }
    [[nodiscard]] bool started() const noexcept { return started_; }

private:
    Handler handler_;
    bool started_ = false;
};

} // namespace lankey::tests
