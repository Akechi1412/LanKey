#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/model/EngineSettings.h"

#include "platform/win32/Win32.h"

namespace lankey::ui::win32 {

// "Kiểu gõ tự định nghĩa", in the spirit of Unikey's user-defined input method dialog:
// a table of (key, function) rows - the eleven engine functions (five tones, three
// circumflex letters, the horn/breve key, đ, remove-mark), each with one to
// kCustomKeysPerFunction keys - that the user extends, edits and trims row by row, seeds
// from a built-in method, and saves to / loads from a text file.
//
// Modal over its owner (the owner is disabled while the dialog is up). The result reaches
// the caller through OnApply only when the user confirms.
//
// Threading: UI thread only.
class CustomMethodDialog {
public:
    using OnApply = std::function<void(const std::string& keys)>; // serialised table

    CustomMethodDialog() = default;
    ~CustomMethodDialog();

    CustomMethodDialog(const CustomMethodDialog&) = delete;
    CustomMethodDialog& operator=(const CustomMethodDialog&) = delete;

    // `keys`: the current EngineSettings::customKeys (see parseCustomKeys()).
    void show(HINSTANCE instance, HWND owner, const std::string& keys, OnApply onApply);
    void destroy();

private:
    struct Row {
        char key = 0;
        int function = 0; // index into the eleven functions
    };

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    void build();
    void rebuildFonts();
    void layout();
    void fillList(int select = -1);
    [[nodiscard]] int selectedRow() const;
    [[nodiscard]] char keyInEdit() const; // 0 when the edit is empty or not a key character
    void addRow();
    void replaceRow();
    void removeRow();
    void removeAll();
    void loadPreset();
    void loadFile();
    void saveFile();
    void setTable(const core::model::CustomKeyTable& table);
    [[nodiscard]] core::model::CustomKeyTable table() const;
    [[nodiscard]] std::wstring problem() const; // empty when the table is a usable method
    void close(bool apply);
    [[nodiscard]] int px(int v) const;

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HWND presetLabel_ = nullptr;
    HWND preset_ = nullptr;
    HWND loadPreset_ = nullptr;
    HWND editLabel_ = nullptr;
    HWND function_ = nullptr;
    HWND keyLabel_ = nullptr;
    HWND key_ = nullptr;
    HWND add_ = nullptr;
    HWND replace_ = nullptr;
    HWND listLabel_ = nullptr;
    HWND list_ = nullptr;
    HWND open_ = nullptr;
    HWND save_ = nullptr;
    HWND remove_ = nullptr;
    HWND removeAll_ = nullptr;
    HWND hint_ = nullptr;
    HWND ok_ = nullptr;
    HWND cancel_ = nullptr;
    HFONT font_ = nullptr;
    HFONT semibold_ = nullptr;
    HBRUSH background_ = nullptr;
    HINSTANCE instance_ = nullptr;
    UINT dpi_ = 96;
    std::vector<Row> rows_; // ordered by function, then as added
    OnApply onApply_;
    bool syncing_ = false; // suppress notifications while controls are filled by code
};

} // namespace lankey::ui::win32
