# LanKey

Bộ gõ tiếng Việt mã nguồn mở cho Windows, hoạt động trên toàn hệ thống, **học thói quen gõ của
người dùng** để gợi ý từ/cụm từ và tự sửa lỗi chính tả cá nhân hoá. Dữ liệu học nằm 100% trên
máy người dùng, không có đồng bộ cloud.

> **Trạng thái:** v0.1 (MVP) đang dogfood — gõ được toàn hệ thống (hook Win32), học cụm từ vào
> SQLite, gợi ý/dự đoán có popup, tự sửa lỗi cá nhân hoá (F2, mặc định mức Thận trọng, Undo bằng
> Backspace/Ctrl+Z), tray icon, Ctrl+Shift bật/tắt. Chưa có: cửa sổ Settings, mã hoá DB.

## Ý tưởng

Các bộ gõ hiện có (Unikey, EVKey, OpenKey) chỉ làm một việc: chuyển Telex/VNI thành chữ có dấu.
LanKey giữ nguyên phần đó (lấy engine từ upstream, không viết lại) và thêm hai thứ:

- **Gợi ý theo lịch sử cá nhân** — ghi nhớ các cụm 1–5 âm tiết hay gõ ("chương trình", "hệ điều
  hành windows"); sau khi gõ space thì **dự đoán từ tiếp theo**, đang gõ dở thì hoàn thành cụm.
  Popup chỉ hiện khi bạn **ngừng gõ ~350 ms** (không nháy); Tab/Enter chọn, ↑↓ đổi, Esc tắt.
- **Tự sửa lỗi cá nhân hoá** — sau khi một âm tiết kết thúc, đối chiếu với từ điển âm tiết chuẩn
  (~6,7k, nhúng trong exe) qua chỉ mục SymSpell trên "khung" âm tiết với khoảng cách có trọng số
  dấu (tra cứu ~4–45 µs), **chỉ sửa khi có đúng một ứng viên tốt nhất**; ứng viên được xếp hạng
  theo khoảng cách, có/không dấu thanh khớp với lỗi, và tần suất chính bạn gõ. Đo trên toàn từ
  điển (`tests/bench/autocorrect_eval_test.cpp`): các lớp lỗi đoán được nguồn (nhầm phím dấu,
  mũ/móc, phím nảy) sửa sai 0.00–0.09 %, sửa đúng 36–77 % khi chưa có lịch sử, ~100 % khi có. Học thêm từ
  thói quen tự sửa của bạn (xoá rồi gõ lại ≥ 4 lần: "sữa lỗi" → "sửa lỗi") vào `correction_map`.
  Mỗi lần sửa hiện "gốc → đã sửa · ⌫ hoàn tác" cạnh con trỏ ~2 s; Backspace/Ctrl+Z ngay sau đó
  hoàn tác — từ gốc được học lại và phỏng đoán đó tạm ngưng 7 ngày, hoàn tác lần nữa thì vào
  blacklist. Không sửa tiếng Anh xen kẽ (âm tiết engine không biến đổi), sau Enter/Tab, trong ô
  mật khẩu; `autoCorrect.excludedApps` (mặc định trống) để tự loại trừ app nếu muốn.

Đơn vị học là **cụm 1–5 âm tiết**, không phải âm tiết đơn — nếu không thì "chương trình" không
bao giờ được học và "sữa lỗi" không bao giờ được sửa.

## Kiến trúc

```
Phím thô → [platform/win32: hook] → [core: engine adapter → smart layer] → [platform: SendInput]
                                          ▲ pure C++20, zero Win32, test được trên Linux
```

| Thư mục | Vai trò | Trạng thái |
|---|---|---|
| `core/` | model, interface, engine adapter, pipeline, smart layer (privacy/learn/suggest/correct), storage (SQLite, JSON), threading. **Không include Win32.** | có |
| `third_party/` | engine upstream vendored, mỗi thư mục có `UPSTREAM.md` + `LICENSE` + `patches/` | `engine-openkey/` |
| `tests/` | `unit/` (một file test cho mỗi class), `fakes/` (một fake cho mỗi interface), `replay/` (harness + `fixtures/`), `support/` | 257 test |
| `tools/` | script sinh dữ liệu (`build-dictionary`) | có |
| `data/` | từ điển âm tiết chuẩn (read-only, sinh từ `tools/`) | 6.683 âm tiết |
| `platform/win32/` | `KeyboardHook` (thread riêng + watchdog), `InputSender`, `FocusWatcher` (UIA IsPassword), `CaretResolver` (UIA caret) | có |
| `ui/win32/` | `TrayIcon`, `SuggestionPopup` (GDI) | có |
| `app/` | `App` (DI thủ công, 4 thread, marshalling), `Log`, `main` | có |

Quy tắc phụ thuộc: `app → ui, platform, core`; `ui → core`; `platform → core`; `core → chỉ STL +
third_party thuần C++`. Cấm `core → platform/ui/<windows.h>` và `ui → platform`.

Các quyết định đã chốt:

- **Engine:** OpenKey (`third_party/engine-openkey/`, GPL-3.0), trích phần engine thuần, không fork
  toàn bộ app. Chọn sau khi chạy cùng một bộ conformance test với VKey: OpenKey 28/28, VKey 26/28
  (bug đặt lại dấu sau Backspace ở kiểu dấu cũ). Mọi code ngoài `core/engine/*Adapter.cpp` chỉ được
  include `core/interfaces/IVietnameseEngine.h`, không bao giờ include header upstream.
- **Engine có global state** → chỉ một `OpenKeyEngineAdapter` mỗi process (constructor throw nếu tạo
  cái thứ hai). Test cần nhiều engine song song phải dùng fake.
- **Bắt phím:** low-level hook + `SendInput` cho v1. TSF sau này là một `IKeySource`/`ITextSink`
  khác, core không đổi.
- **Thread:** hook (< 1 ms, `noexcept`, không I/O/lock) · worker · DB · UI. Mọi lệnh thay thế văn
  bản sinh ra ngoài hook thread mang `expectedGeneration`; lệch với generation hiện tại → bỏ. Đây là
  cách duy nhất ngăn autocorrect bất đồng bộ "ăn chữ" khi gõ nhanh.
- **Lưu trữ:** SQLite tại `%APPDATA%\LanKey\` sau `ILexiconStore`; SQLCipher + DPAPI thêm sau.
- **Engine không nhận diện được tiếng Anh xen kẽ** khi đang gõ (`text` → `tẽt` vì `x` là phím ngã;
  chỉ khôi phục `text` tại space). Việc đó thuộc Smart Layer, qua cờ
  `ComposedText::vietnameseTransformApplied` + từ điển.

## Build

Cần CMake ≥ 3.25, Ninja, vcpkg (biến môi trường `VCPKG_ROOT`) và một trong hai toolchain:

**MSVC 2022** (workload *Desktop development with C++*):

```powershell
cmake --preset win
cmake --build --preset win-debug
ctest --preset win-debug
```

**LLVM-MinGW** (không cần admin, không cần Visual Studio —
`winget install MartinStorsjo.LLVM-MinGW.UCRT --scope user`):

```powershell
cmake --preset win-clang
cmake --build --preset win-clang-debug
ctest --preset win-clang-debug
```

Lần configure đầu vcpkg sẽ build `gtest` (~1 phút). Engine OpenKey được bật mặc định
(`LANKEY_ENGINE_OPENKEY=ON`).

Từ điển âm tiết chuẩn: `python tools/build-dictionary/build_syllables.py` sinh
`data/vi_base_syllables.txt` (nguồn và license trong `tools/build-dictionary/SOURCES.md`).

## Test

```
tests/unit/       một file cho mỗi class public trong core/; engine_conformance.cpp chạy 28 case
                  cho MỌI adapter đăng ký trong engine_registry.cpp
tests/fakes/      FakeKeySource, FakeTextSink, FakeFocusObserver, FakeClock, FakeEngine,
                  InMemoryLexiconStore, ... - một fake cho mỗi interface trong core/interfaces/
tests/replay/     ReplayHarness: chạy một chuỗi phím ghi sẵn qua toàn bộ InputPipeline với engine
                  THẬT và platform giả; mỗi thư mục con của fixtures/ là một test
```

Mỗi bug thực tế gặp khi dùng thử → **ghi thành một fixture trước khi sửa**. Một fixture gồm:

```
tests/replay/fixtures/<tên>/
  keys.keylog    JSON lines, mỗi dòng một sự kiện, dòng bắt đầu bằng # là chú thích:
                   {"text":"chaof banj."}              chuỗi phím (chữ hoa = Shift)
                   {"k":"Backspace"}                   phím có tên: Backspace Enter Tab Escape Space
                                                       Left Right Up Down Home End Delete
                   {"k":"Tab","mods":["Alt"]}          kèm Ctrl / Alt / Win / Shift / CapsLock
                   {"focus":{"app":"chrome.exe","password":false}}
                   {"click":true}
                   {"wait":1500}                       tua đồng hồ giả (ms)
  expected.txt   nội dung màn hình cuối cùng
  commits.txt    (tuỳ chọn) mỗi dòng một âm tiết đã commit: <cửa sổ cụm>|<terminator>|<transform 0/1>
                 terminator: SP, NL, TAB, NONE hoặc chính ký tự đó. Ví dụ: chào bạn|.|1
```

## Quy ước code

- C++20, `-Wall -Wextra -Werror` (MSVC: `/W4 /WX`). Code upstream trong `third_party/` build với
  cờ riêng, không áp `-Werror`.
- Comment trong code viết bằng **tiếng Anh**; giải thích *tại sao*, edge case tiếng Việt kèm ví dụ.
- Namespace `lankey::core::{model,engine,...}`, `lankey::platform::win32`, `lankey::ui`, `lankey::app`.
  Kiểu `PascalCase`, hàm/biến `camelCase`, hằng `kPascalCase`, member `name_`.
- Chuỗi nội bộ core là `std::u32string` đã NFC; đổi sang UTF-16 chỉ ở biên platform/ui.
- Mỗi class ghi rõ đầu file chạy trên thread nào. Không singleton, không global mutable state
  (trừ những gì adapter phải che đi của upstream).
- Mỗi class public trong `core/` có file test cùng tên. Không test qua Win32.
- `.clang-format` và `.clang-tidy` ở root. CI chạy `clang-format --dry-run --Werror` trên `core/` và
  `tests/`; chạy local: `git ls-files 'core/*' 'tests/*' | grep -E '\.(h|cpp)$' | xargs clang-format -i`.
  `clang-tidy -p build/<preset> <file>` dùng `compile_commands.json` của preset.
- Thread: hook thread không I/O, không lock, không ném exception (`InputPipeline::onKey` là
  `noexcept`). Mọi `TextReplacement` sinh ngoài hook thread mang `expectedGeneration`.

## Thêm engine mới

1. Vendored vào `third_party/engine-<tên>/` theo mẫu `engine-openkey/` (`UPSTREAM.md`, `LICENSE`,
   `patches/`, `CMakeLists.txt` không link `lankey_options`, include dir `SYSTEM`).
2. Viết `core/engine/<Tên>EngineAdapter.{h,cpp}` implement `IVietnameseEngine`.
3. Thêm option + target trong `core/CMakeLists.txt`, đăng ký trong `tests/unit/engine_registry.cpp`.
4. Chạy `lankey_tests`: mọi case trong `engine_conformance.cpp` phải pass.

## Giấy phép

GPL-3.0-or-later. Engine xử lý tiếng Việt lấy từ dự án upstream (ghi rõ trong
`third_party/*/UPSTREAM.md`). Từ điển âm tiết chuẩn sinh từ hunspell-vi / wordlist của Hồ Ngọc Đức
(GPL, xem `tools/build-dictionary/SOURCES.md`). Quy tắc âm vị học tiếng Việt tham khảo từ Unikey
của Phạm Kim Long.
