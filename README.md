# LanKey

Bộ gõ tiếng Việt mã nguồn mở cho Windows, hoạt động trên toàn hệ thống, **học thói quen gõ của
người dùng** để gợi ý từ/cụm từ và tự sửa lỗi chính tả cá nhân hoá. Dữ liệu học nằm 100% trên
máy người dùng, không có đồng bộ cloud.

> **Trạng thái:** đang phát triển, chưa gõ được. Đã có engine Telex/VNI (OpenKey) bọc sau
> interface riêng và bộ conformance test; chưa có hook bàn phím, chưa có Smart Layer.

## Ý tưởng

Các bộ gõ hiện có (Unikey, EVKey, OpenKey) chỉ làm một việc: chuyển Telex/VNI thành chữ có dấu.
LanKey giữ nguyên phần đó (lấy engine từ upstream, không viết lại) và thêm hai thứ:

- **Gợi ý theo lịch sử cá nhân** — ghi nhớ các cụm 1–3 âm tiết hay gõ ("chương trình", "hệ điều
  hành"), hiện popup khi gõ vài ký tự đầu, chọn bằng Tab.
- **Tự sửa lỗi cá nhân hoá** — phát hiện âm tiết/cụm gõ sai so với từ điển chuẩn hoặc so với chính
  thói quen tự sửa của người dùng ("sữa lỗi" → "sửa lỗi"), có Undo bằng Backspace.

Đơn vị học là **cụm 1–3 âm tiết**, không phải âm tiết đơn — nếu không thì "chương trình" không
bao giờ được học và "sữa lỗi" không bao giờ được sửa.

## Kiến trúc

```
Phím thô → [platform/win32: hook] → [core: engine adapter → smart layer] → [platform: SendInput]
                                          ▲ pure C++20, zero Win32, test được trên Linux
```

| Thư mục | Vai trò | Trạng thái |
|---|---|---|
| `core/` | model, interface, engine adapter, smart layer, storage. **Không include Win32.** | model + `IVietnameseEngine` + OpenKey adapter |
| `third_party/` | engine upstream vendored, mỗi thư mục có `UPSTREAM.md` + `LICENSE` + `patches/` | `engine-openkey/` |
| `tests/` | unit test + conformance test cho engine; sau này thêm replay harness | conformance 28 case, Typist |
| `tools/` | script sinh dữ liệu (`build-dictionary`) | có |
| `data/` | từ điển âm tiết chuẩn (read-only, sinh từ `tools/`) | 6.683 âm tiết |
| `platform/win32/` | hook, SendInput, UIA, DPAPI — implement interface của `core/` | chưa có |
| `ui/` | popup gợi ý, tray, settings | chưa có |
| `app/` | `main()`, nối mọi thứ lại (DI thủ công) | chưa có |

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
- `.clang-format` và `.clang-tidy` ở root; chạy `clang-format --dry-run --Werror` trước khi commit.

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
