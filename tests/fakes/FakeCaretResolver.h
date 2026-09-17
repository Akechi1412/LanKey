#pragma once

#include <optional>

#include "core/interfaces/ICaretResolver.h"

namespace lankey::tests {

class FakeCaretResolver final : public core::ICaretResolver {
public:
    [[nodiscard]] std::optional<core::model::ScreenRect> resolve() override {
        ++calls_;
        return rect_;
    }

    void setRect(std::optional<core::model::ScreenRect> rect) { rect_ = rect; }
    [[nodiscard]] int calls() const noexcept { return calls_; }

private:
    std::optional<core::model::ScreenRect> rect_;
    int calls_ = 0;
};

} // namespace lankey::tests
