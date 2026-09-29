#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "platform/win32/Win32.h"

namespace lankey::ui::win32 {

// The little window behind "Thêm" and "Sửa" on the glossary and snippet pages: a stack of
// labelled fields, OK and Huỷ.
//
// One editor for both because the two pages differ only in which fields they ask for -
// four one-line boxes for a glossary term, an abbreviation plus a body plus a flag for a
// snippet. Giving each page its own dialog would mean maintaining the same window twice.
//
// Modal in the same way CustomMethodDialog is: the owner is disabled while it is up and
// the result arrives through the callback, so the application keeps one message loop.
//
// Threading: UI thread only.
class RowEditor {
public:
    struct Field {
        enum class Kind : std::uint8_t {
            Line,      // one-line text
            Multiline, // several lines, for a snippet body
            Check,     // a flag; the value is L"1" or empty
        };
        std::wstring label;
        std::wstring value;
        Kind kind = Kind::Line;
        std::wstring hint; // dim text under the field, optional
    };
    // The edited values, in the order the fields were given.
    using OnApply = std::function<void(const std::vector<std::wstring>& values)>;

    RowEditor() = default;
    ~RowEditor();

    RowEditor(const RowEditor&) = delete;
    RowEditor& operator=(const RowEditor&) = delete;

    void show(HINSTANCE instance, HWND owner, const std::wstring& title, std::vector<Field> fields,
              OnApply onApply);
    void destroy();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    void close(bool apply);
    [[nodiscard]] int px(int v) const;

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HWND ok_ = nullptr;
    HWND cancel_ = nullptr;
    HFONT font_ = nullptr;
    HFONT semibold_ = nullptr;
    HBRUSH background_ = nullptr;
    UINT dpi_ = 96;
    std::vector<Field> fields_;
    std::vector<HWND> controls_; // one per field, same order
    std::vector<HWND> dim_;      // labels and hints, drawn grey
    OnApply onApply_;
};

} // namespace lankey::ui::win32
