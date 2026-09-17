#pragma once

#include <memory>
#include <utility>

#include "core/interfaces/IFocusObserver.h"

namespace lankey::tests {

class FakeFocusObserver final : public core::IFocusObserver {
public:
    FakeFocusObserver() : current_(std::make_shared<const core::model::FocusContext>()) {}

    [[nodiscard]] std::shared_ptr<const core::model::FocusContext> current() const override {
        return current_;
    }
    void onChange(ChangeHandler handler) override { handler_ = std::move(handler); }

    // Test side: move focus and notify like the platform would.
    void setFocus(core::model::FocusContext focus) {
        current_ = std::make_shared<const core::model::FocusContext>(std::move(focus));
        if (handler_) handler_(*current_);
    }

private:
    std::shared_ptr<const core::model::FocusContext> current_;
    ChangeHandler handler_;
};

} // namespace lankey::tests
