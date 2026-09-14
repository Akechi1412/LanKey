# LanKey

Bộ gõ tiếng Việt mã nguồn mở cho Windows, hoạt động trên toàn hệ thống, **học thói quen gõ của
người dùng** để gợi ý từ/cụm từ và tự sửa lỗi chính tả cá nhân hoá. Dữ liệu học nằm 100% trên
máy người dùng.

> **Trạng thái:** Phase 0 — nghiên cứu engine upstream, chưa dùng được.
> Kế hoạch đầy đủ: [docs/PLAN.md](docs/PLAN.md). Quyết định kiến trúc: [docs/adr/](docs/adr/).

## Kiến trúc trong một hình

```
Phím thô → [platform/win32: hook] → [core: engine adapter → smart layer] → [platform: SendInput]
                                          ▲ pure C++20, zero Win32, test trên Linux
```

- `core/` — model, interface, engine adapter, smart layer, storage. **Không include Win32.**
- `platform/win32/` — hook, SendInput, UIA, DPAPI. Implement các interface của `core/`.
- `ui/` — popup gợi ý, tray, settings.
- `app/` — `main()`, nối mọi thứ lại (DI thủ công).
- `third_party/` — engine Telex/VNI lấy từ upstream (vendored, kèm `UPSTREAM.md` + `patches/`).
- `tests/` — unit test + replay harness; chạy trên Windows và Linux.

## Build

Yêu cầu: Visual Studio 2022 với workload *Desktop development with C++* (gồm CMake + vcpkg),
biến môi trường `VCPKG_ROOT`.

```powershell
cmake --preset win
cmake --build --preset win-debug
ctest --preset win-debug
```

Bật adapter engine để chạy conformance test (Phase 0):

```powershell
cmake --preset win -DLANKEY_ENGINE_OPENKEY=ON
```

## Giấy phép

GPL-3.0-or-later. Engine xử lý tiếng Việt lấy từ dự án upstream (ghi rõ trong
`third_party/*/UPSTREAM.md`). Quy tắc âm vị học tiếng Việt tham khảo từ Unikey của Phạm Kim Long.
