#pragma once

#include <functional>
#include <string>

#include "platform/win32/Win32.h"

namespace lankey::ui::win32 {

// "Dữ liệu của bạn": the data policy, verbatim from DATA-POLICY.md, with the two actions
// it promises - open the data folder, erase everything. A plain top-level window with a
// read-only text box; created on demand, one at a time.
//
// Threading: UI thread only.
class DataDialog {
public:
    struct Callbacks {
        std::function<void()> onOpenFolder;
        std::function<void()> onEraseAllData;
    };

    DataDialog() = default;
    ~DataDialog();

    DataDialog(const DataDialog&) = delete;
    DataDialog& operator=(const DataDialog&) = delete;

    // Shows the dialog (creating it on first use) with `text` as its body; brings an
    // already open one to the front.
    void show(HINSTANCE instance, const std::wstring& text, Callbacks callbacks);
    void destroy();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    void layout();

    HWND hwnd_ = nullptr;
    HWND text_ = nullptr;
    HWND openFolder_ = nullptr;
    HWND erase_ = nullptr;
    HWND close_ = nullptr;
    HFONT font_ = nullptr;
    Callbacks callbacks_;
};

} // namespace lankey::ui::win32
