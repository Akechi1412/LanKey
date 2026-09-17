#pragma once

namespace lankey::core::model {

// Screen rectangle in physical pixels; used to place the suggestion popup near the caret.
struct ScreenRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const ScreenRect&, const ScreenRect&) = default;
};

} // namespace lankey::core::model
