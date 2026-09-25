#include "ui/win32/CustomMethodDialog.h"

#include <algorithm>
#include <cctype>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <string>
#include <utility>

#include "ui/win32/Theme.h"

namespace lankey::ui::win32 {

namespace {

constexpr wchar_t kClassName[] = L"LanKeyCustomMethodDialog";
constexpr int kWidth = 640;
constexpr int kHeight = 640;
constexpr int kMargin = 16;
constexpr int kRowHeight = 26;
constexpr int kButtonHeight = 30;
constexpr int kSideColumn = 124; // buttons to the right of the list

constexpr COLORREF kBg = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kText = RGB(0x1B, 0x1F, 0x26);
constexpr COLORREF kDim = RGB(0x6B, 0x73, 0x80);

enum Id : int {
    kPreset = 100,
    kLoadPreset,
    kFunction,
    kKey,
    kAdd,
    kReplace,
    kList,
    kOpen,
    kSave,
    kRemove,
    kRemoveAll,
    kOk,
    kCancel,
};

using core::model::CustomKeyTable;
using core::model::kCustomKeyChars;
using core::model::kCustomKeyCount;
using core::model::kCustomKeysPerFunction;
using core::model::kCustomRequiredCount;

// One entry per engine function, in EngineSettings::customKeys order.
const wchar_t* kFunctions[kCustomKeyCount] = {
    L"Dấu sắc",
    L"Dấu huyền",
    L"Dấu hỏi",
    L"Dấu ngã",
    L"Dấu nặng",
    L"Dấu mũ: A thành Â",
    L"Dấu mũ: O thành Ô",
    L"Dấu mũ: E thành Ê",
    L"Dấu móc: U thành Ư, O thành Ơ, A thành Ă, hoặc tạo chữ Ư",
    L"Dấu gạch: D thành Đ",
    L"Xoá dấu đang có",
    L"Chữ ơ  (giữ Shift: Ơ)",
    L"Chữ ư  (giữ Shift: Ư)",
};

struct Preset {
    const wchar_t* name;
    const char* keys;
};
const Preset kPresets[] = {
    {L"Telex", "s,f,r,x,j,a,o,e,w,d,z,[,]"},
    {L"Telex, không dùng [ ]", "s,f,r,x,j,a,o,e,w,d,z,,"},
    {L"VNI", "1,2,3,4,5,6,6,7,8,9,0,,"},
    {L"Telex và VNI cùng lúc", "s1,f2,r3,x4,j5,a6,o6,e7,w8,d9,z0,[,]"},
};

bool isKeyChar(char c) {
    return kCustomKeyChars.find(c) != std::string_view::npos;
}

std::wstring keyLabel(char key) {
    return std::wstring(1, static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(key))));
}

} // namespace

CustomMethodDialog::~CustomMethodDialog() {
    destroy();
}

int CustomMethodDialog::px(int v) const {
    return MulDiv(v, static_cast<int>(dpi_), 96);
}

void CustomMethodDialog::show(HINSTANCE instance, HWND owner, const std::string& keys,
                              OnApply onApply) {
    instance_ = instance;
    owner_ = owner;
    onApply_ = std::move(onApply);
    if (hwnd_ == nullptr) build();
    if (hwnd_ == nullptr) return;
    auto table = core::model::parseCustomKeys(keys);
    if (!table) table = core::model::parseCustomKeys(core::model::kDefaultCustomKeys);
    setTable(*table);
    EnableWindow(owner_, FALSE);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    SetFocus(list_);
}

void CustomMethodDialog::destroy() {
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    for (const HFONT f : {font_, semibold_}) {
        if (f != nullptr) DeleteObject(f);
    }
    font_ = semibold_ = nullptr;
    if (background_ != nullptr) DeleteObject(background_);
    background_ = nullptr;
}

void CustomMethodDialog::build() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &CustomMethodDialog::wndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    dpi_ = owner_ != nullptr ? GetDpiForWindow(owner_) : GetDpiForSystem();
    background_ = CreateSolidBrush(kBg);

    RECT ownerRect{};
    if (owner_ == nullptr || !GetWindowRect(owner_, &ownerRect)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRect, 0);
    }
    const int w = px(kWidth);
    const int h = px(kHeight);
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, L"Kiểu gõ tự định nghĩa",
                            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                            (ownerRect.left + ownerRect.right - w) / 2,
                            (ownerRect.top + ownerRect.bottom - h) / 2, w, h, owner_, nullptr,
                            instance_, this);
    if (hwnd_ == nullptr) return;
    setWindowIcons(hwnd_, instance_);
    enableDialogNavigation(hwnd_);

    const auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id,
                          DWORD exStyle = 0) {
        return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                               hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_,
                               nullptr);
    };
    presetLabel_ = make(L"STATIC", L"Bắt đầu từ kiểu gõ có sẵn", SS_LEFT, 0);
    preset_ = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kPreset);
    for (const auto& p : kPresets)
        SendMessageW(preset_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.name));
    SendMessageW(preset_, CB_SETCURSEL, 0, 0);
    loadPreset_ = make(L"BUTTON", L"Nạp kiểu gõ này", BS_PUSHBUTTON | WS_TABSTOP, kLoadPreset);

    editLabel_ = make(L"STATIC", L"Định nghĩa phím", SS_LEFT, 0);
    function_ = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kFunction);
    for (const auto* f : kFunctions)
        SendMessageW(function_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(f));
    SendMessageW(function_, CB_SETCURSEL, 0, 0);
    keyLabel_ = make(L"STATIC", L"Phím", SS_LEFT, 0);
    key_ = make(L"EDIT", L"", ES_LOWERCASE | ES_CENTER | WS_TABSTOP, kKey, WS_EX_CLIENTEDGE);
    SendMessageW(key_, EM_SETLIMITTEXT, 1, 0);
    add_ = make(L"BUTTON", L"Thêm vào", BS_PUSHBUTTON | WS_TABSTOP, kAdd);
    replace_ = make(L"BUTTON", L"Thay thế", BS_PUSHBUTTON | WS_TABSTOP, kReplace);

    listLabel_ = make(L"STATIC", L"Phím và tính năng", SS_LEFT, 0);
    list_ = make(WC_LISTVIEWW, L"",
                 LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP,
                 kList, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* headers[] = {L"Phím", L"Tính năng"};
    for (int c = 0; c < 2; ++c) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(headers[c]);
        col.cx = 10;
        ListView_InsertColumn(list_, c, &col);
    }
    open_ = make(L"BUTTON", L"Chọn file", BS_PUSHBUTTON | WS_TABSTOP, kOpen);
    save_ = make(L"BUTTON", L"Ghi file", BS_PUSHBUTTON | WS_TABSTOP, kSave);
    remove_ = make(L"BUTTON", L"Xoá phím", BS_PUSHBUTTON | WS_TABSTOP, kRemove);
    removeAll_ = make(L"BUTTON", L"Xoá tất cả", BS_PUSHBUTTON | WS_TABSTOP, kRemoveAll);

    hint_ = make(L"STATIC",
                 L"Chọn tính năng, gõ phím rồi bấm Thêm vào; chọn một dòng để Thay thế hoặc Xoá "
                 L"phím. Mỗi tính năng tối đa 4 phím; ba dấu mũ có thể dùng chung một phím như "
                 L"VNI; chữ ơ/ư có thể bỏ trống. Phím: chữ cái, chữ số hoặc [ ] ; ' . / \\ - = `",
                 SS_LEFT, 0);
    ok_ = make(L"BUTTON", L"Đồng ý", BS_DEFPUSHBUTTON | WS_TABSTOP, kOk);
    cancel_ = make(L"BUTTON", L"Huỷ", BS_PUSHBUTTON | WS_TABSTOP, kCancel);

    rebuildFonts();
    layout();
}

void CustomMethodDialog::rebuildFonts() {
    for (const HFONT f : {font_, semibold_}) {
        if (f != nullptr) DeleteObject(f);
    }
    font_ = createUiFont(dpi_, 10, FW_NORMAL);
    semibold_ = createUiFont(dpi_, 10, FW_SEMIBOLD);
    for (const HWND h : {preset_, loadPreset_, function_, keyLabel_, key_, add_, replace_, list_,
                         open_, save_, remove_, removeAll_, hint_, ok_, cancel_}) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    for (const HWND h : {presetLabel_, editLabel_, listLabel_}) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(semibold_), TRUE);
    }
}

void CustomMethodDialog::layout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int margin = px(kMargin);
    const int width = client.right - 2 * margin;
    const int row = px(kRowHeight);
    const int button = px(kButtonHeight);
    const int side = px(kSideColumn);
    const int sideX = client.right - margin - side;
    int y = margin;

    // Bottom up: the OK/Cancel row and the hint claim their space first, the list takes
    // what is left so nothing can overlap at any DPI.
    const int buttonsY = client.bottom - margin - button;
    const int hintH = px(54);
    const int hintY = buttonsY - px(12) - hintH;

    MoveWindow(presetLabel_, margin, y, width, row, TRUE);
    y += row + px(6);
    MoveWindow(preset_, margin, y, px(260), px(220), TRUE);
    MoveWindow(loadPreset_, margin + px(270), y - px(2), px(150), button, TRUE);
    y += button + px(16);

    MoveWindow(editLabel_, margin, y, width, row, TRUE);
    y += row + px(6);
    MoveWindow(function_, margin, y, px(360), px(280), TRUE);
    y += row + px(8);
    MoveWindow(keyLabel_, margin, y + px(4), px(40), row, TRUE);
    MoveWindow(key_, margin + px(44), y, px(48), row, TRUE);
    MoveWindow(add_, margin + px(104), y - px(2), px(110), button, TRUE);
    MoveWindow(replace_, margin + px(224), y - px(2), px(110), button, TRUE);
    y += button + px(16);

    MoveWindow(listLabel_, margin, y, width, row, TRUE);
    y += row + px(6);
    const int listWidth = width - side - px(10);
    const int listHeight = std::max(px(120), hintY - px(12) - y);
    MoveWindow(list_, margin, y, listWidth, listHeight, TRUE);
    ListView_SetColumnWidth(list_, 0, px(64));
    ListView_SetColumnWidth(list_, 1, listWidth - px(64) - px(24));
    MoveWindow(open_, sideX, y, side, button, TRUE);
    MoveWindow(save_, sideX, y + button + px(8), side, button, TRUE);
    MoveWindow(remove_, sideX, y + listHeight - 2 * button - px(8), side, button, TRUE);
    MoveWindow(removeAll_, sideX, y + listHeight - button, side, button, TRUE);

    MoveWindow(hint_, margin, hintY, width, hintH, TRUE);
    MoveWindow(ok_, client.right - margin - px(220), buttonsY, px(104), button, TRUE);
    MoveWindow(cancel_, client.right - margin - px(104), buttonsY, px(104), button, TRUE);
}

// -- table <-> rows -----------------------------------------------------------------------------

void CustomMethodDialog::setTable(const CustomKeyTable& table) {
    rows_.clear();
    for (int f = 0; f < kCustomKeyCount; ++f) {
        for (const char c : table[static_cast<std::size_t>(f)])
            rows_.push_back({c, f});
    }
    fillList(rows_.empty() ? -1 : 0);
}

CustomKeyTable CustomMethodDialog::table() const {
    CustomKeyTable t;
    for (const Row& r : rows_)
        t[static_cast<std::size_t>(r.function)].push_back(r.key);
    return t;
}

void CustomMethodDialog::fillList(int select) {
    syncing_ = true;
    std::stable_sort(rows_.begin(), rows_.end(),
                     [](const Row& a, const Row& b) { return a.function < b.function; });
    ListView_DeleteAllItems(list_);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const std::wstring key = keyLabel(rows_[i].key);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(key.c_str());
        ListView_InsertItem(list_, &item);
        ListView_SetItemText(list_, static_cast<int>(i), 1,
                             const_cast<wchar_t*>(kFunctions[rows_[i].function]));
    }
    syncing_ = false;
    if (select >= 0 && select < static_cast<int>(rows_.size())) {
        ListView_SetItemState(list_, select, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list_, select, FALSE);
    }
    const bool any = !rows_.empty();
    EnableWindow(remove_, any && selectedRow() >= 0);
    EnableWindow(replace_, any && selectedRow() >= 0);
    EnableWindow(removeAll_, any);
}

int CustomMethodDialog::selectedRow() const {
    return ListView_GetNextItem(list_, -1, LVNI_SELECTED);
}

char CustomMethodDialog::keyInEdit() const {
    wchar_t buf[4] = {};
    GetWindowTextW(key_, buf, 3);
    if (buf[0] == 0 || buf[0] >= 128) return 0;
    const auto c = static_cast<char>(std::tolower(static_cast<int>(buf[0])));
    return isKeyChar(c) ? c : 0;
}

// -- row editing ----------------------------------------------------------------------------------

void CustomMethodDialog::addRow() {
    const char key = keyInEdit();
    const auto function = static_cast<int>(SendMessageW(function_, CB_GETCURSEL, 0, 0));
    if (key == 0) {
        MessageBoxW(hwnd_, L"Gõ một phím vào ô Phím: chữ cái, chữ số hoặc [ ] ; ' . / \\ - = `",
                    L"LanKey", MB_ICONINFORMATION | MB_OK);
        SetFocus(key_);
        return;
    }
    if (function < 0 || function >= kCustomKeyCount) return;
    int count = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].function == function) ++count;
        if (rows_[i].function == function && rows_[i].key == key) {
            fillList(static_cast<int>(i)); // already there: just show it
            return;
        }
    }
    if (count >= kCustomKeysPerFunction) {
        MessageBoxW(hwnd_, L"Mỗi tính năng tối đa 4 phím.", L"LanKey", MB_ICONINFORMATION | MB_OK);
        return;
    }
    rows_.push_back({key, function});
    fillList(static_cast<int>(rows_.size()) - 1);
    // fillList() re-sorts: find the row again to select it.
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].function == function && rows_[i].key == key) {
            fillList(static_cast<int>(i));
            break;
        }
    }
    SetWindowTextW(key_, L"");
    SetFocus(key_);
}

void CustomMethodDialog::replaceRow() {
    const int row = selectedRow();
    const char key = keyInEdit();
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        MessageBoxW(hwnd_, L"Chọn dòng cần thay trong bảng trước.", L"LanKey",
                    MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (key == 0) {
        MessageBoxW(hwnd_, L"Gõ phím mới vào ô Phím: chữ cái, chữ số hoặc [ ] ; ' . / \\ - = `",
                    L"LanKey", MB_ICONINFORMATION | MB_OK);
        SetFocus(key_);
        return;
    }
    const auto function = static_cast<int>(SendMessageW(function_, CB_GETCURSEL, 0, 0));
    rows_[static_cast<std::size_t>(row)] = {key, function};
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (static_cast<int>(i) != row && rows_[i].function == function && rows_[i].key == key) {
            rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(i)); // now a duplicate
            break;
        }
    }
    fillList(row);
}

void CustomMethodDialog::removeRow() {
    const int row = selectedRow();
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        MessageBoxW(hwnd_, L"Chọn dòng cần xoá trong bảng trước.", L"LanKey",
                    MB_ICONINFORMATION | MB_OK);
        return;
    }
    rows_.erase(rows_.begin() + row);
    fillList(std::min(row, static_cast<int>(rows_.size()) - 1));
}

void CustomMethodDialog::removeAll() {
    rows_.clear();
    fillList();
}

void CustomMethodDialog::loadPreset() {
    const auto sel = static_cast<int>(SendMessageW(preset_, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(std::size(kPresets))) return;
    if (const auto table = core::model::parseCustomKeys(kPresets[sel].keys)) setTable(*table);
}

// -- files
// ------------------------------------------------------------------------------------------

void CustomMethodDialog::loadFile() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Kiểu gõ LanKey (*.txt)\0*.txt\0Mọi tệp (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    // The first non-comment line is the serialised table (what saveFile() writes).
    std::ifstream in(path);
    std::string line;
    std::optional<CustomKeyTable> table;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        if (i >= line.size() || line[i] == '#') continue;
        table = core::model::parseCustomKeys(line.substr(i));
        break;
    }
    if (!table) {
        MessageBoxW(hwnd_,
                    L"Tệp không phải kiểu gõ LanKey: cần một dòng 13 nhóm phím cách nhau bằng "
                    L"dấu phẩy, theo thứ tự sắc, huyền, hỏi, ngã, nặng, â, ô, ê, ư/ơ/ă, đ, xoá "
                    L"dấu, chữ ơ, chữ ư (hai nhóm cuối có thể để trống).",
                    L"LanKey", MB_ICONWARNING | MB_OK);
        return;
    }
    setTable(*table);
}

void CustomMethodDialog::saveFile() {
    wchar_t path[MAX_PATH] = L"kieu-go-lankey.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Kiểu gõ LanKey (*.txt)\0*.txt\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    std::ofstream out(path, std::ios::binary);
    out << platform::win32::toUtf8(
        L"# LanKey - kiểu gõ tự định nghĩa. Một dòng, 13 nhóm phím cách nhau bằng dấu phẩy,\n"
        L"# theo thứ tự: sắc, huyền, hỏi, ngã, nặng, â, ô, ê, ư/ơ/ă, đ, xoá dấu, chữ ơ, chữ ư.\n");
    out << core::model::joinCustomKeys(table()) << '\n';
}

// -- confirm
// ---------------------------------------------------------------------------------------

std::wstring CustomMethodDialog::problem() const {
    const CustomKeyTable t = table();
    for (int f = 0; f < kCustomRequiredCount; ++f) {
        if (t[static_cast<std::size_t>(f)].empty()) {
            return std::wstring(L"Chưa có phím cho: ") + kFunctions[f] +
                   L".\nThêm phím cho tính năng này hoặc nạp một kiểu gõ có sẵn rồi sửa.";
        }
    }
    for (int i = 0; i < kCustomKeyCount; ++i) {
        for (int j = i + 1; j < kCustomKeyCount; ++j) {
            const bool circumflex = i >= 5 && i <= 7 && j >= 5 && j <= 7;
            if (circumflex) continue; // VNI-style shared key, resolved by the vowel typed
            for (const char c : t[static_cast<std::size_t>(i)]) {
                if (t[static_cast<std::size_t>(j)].find(c) != std::string::npos) {
                    return L"Phím " + keyLabel(c) + L" được dùng cho hai tính năng: " +
                           kFunctions[i] + L" và " + kFunctions[j] + L".";
                }
            }
        }
    }
    return L"";
}

void CustomMethodDialog::close(bool apply) {
    if (apply) {
        if (const std::wstring why = problem(); !why.empty()) {
            MessageBoxW(hwnd_, why.c_str(), L"LanKey", MB_ICONWARNING | MB_OK);
            return;
        }
    }
    EnableWindow(owner_, TRUE);
    ShowWindow(hwnd_, SW_HIDE);
    if (owner_ != nullptr) SetForegroundWindow(owner_);
    if (apply && onApply_) onApply_(core::model::joinCustomKeys(table()));
}

// -- messages
// --------------------------------------------------------------------------------------

LRESULT CALLBACK CustomMethodDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        static_cast<CustomMethodDialog*>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<CustomMethodDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) return self->handle(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CustomMethodDialog::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, background_);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, reinterpret_cast<HWND>(lParam) == hint_ ? kDim : kText);
        return reinterpret_cast<LRESULT>(background_);
    }
    case WM_SETCURSOR: {
        const auto over = reinterpret_cast<HWND>(wParam);
        wchar_t cls[16] = {};
        GetClassNameW(over, cls, 15);
        if (over != nullptr && over != hwnd_ && _wcsicmp(cls, L"Button") == 0 &&
            IsWindowEnabled(over)) {
            showHandCursor();
            return TRUE;
        }
        break;
    }
    case WM_NOTIFY: {
        const auto* hdr = reinterpret_cast<const NMHDR*>(lParam);
        if (hdr->hwndFrom != list_ || syncing_) break;
        if (hdr->code == LVN_ITEMCHANGED) {
            const auto* lv = reinterpret_cast<const NMLISTVIEW*>(lParam);
            if ((lv->uNewState & LVIS_SELECTED) != 0 && lv->iItem >= 0 &&
                lv->iItem < static_cast<int>(rows_.size())) {
                // The selected row becomes the editing row: function and key follow it.
                const Row& r = rows_[static_cast<std::size_t>(lv->iItem)];
                SendMessageW(function_, CB_SETCURSEL, static_cast<WPARAM>(r.function), 0);
                SetWindowTextW(key_, keyLabel(r.key).c_str());
                EnableWindow(remove_, TRUE);
                EnableWindow(replace_, TRUE);
            }
            return 0;
        }
        if (hdr->code == NM_DBLCLK) {
            SetFocus(key_);
            SendMessageW(key_, EM_SETSEL, 0, -1);
            return 0;
        }
        if (hdr->code == LVN_KEYDOWN) {
            const auto* kd = reinterpret_cast<const NMLVKEYDOWN*>(lParam);
            if (kd->wVKey == VK_DELETE) removeRow();
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        switch (id) {
        case kLoadPreset:
            loadPreset();
            return 0;
        case kAdd:
            addRow();
            return 0;
        case kReplace:
            replaceRow();
            return 0;
        case kRemove:
            removeRow();
            return 0;
        case kRemoveAll:
            removeAll();
            return 0;
        case kOpen:
            loadFile();
            return 0;
        case kSave:
            saveFile();
            return 0;
        case kOk:
            close(true);
            return 0;
        case kCancel:
        case IDCANCEL: // Esc through IsDialogMessage
            close(false);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_SIZE:
        if (list_ != nullptr && wParam != SIZE_MINIMIZED) layout();
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        rebuildFonts();
        const auto* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case DM_GETDEFID: // IsDialogMessage: Enter adds the key while typing one, else confirms
        return MAKELRESULT(GetFocus() == key_ ? kAdd : kOk, DC_HASDEFID);
    case WM_CLOSE:
        close(false);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace lankey::ui::win32
