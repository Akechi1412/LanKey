# third_party/

Code không phải của LanKey. Mỗi thư mục con phải có:

- `UPSTREAM.md` — repo gốc, commit hash, ngày lấy, license, phần nào của upstream được lấy
  (chỉ engine, không lấy UI/platform).
- `LICENSE` — nguyên văn từ upstream.
- `patches/` — mọi sửa đổi so với upstream dưới dạng `.patch`, đánh số, mỗi patch một lý do.
  Lý tưởng là thư mục rỗng.
- `CMakeLists.txt` — target riêng, **không** link `lankey_options` (không áp `/WX` lên code
  upstream).

Cập nhật upstream: thay `src/`, apply lại `patches/`, chạy `lankey_tests`. Patch không apply
được = chỗ cần xem lại.

Không code nào ngoài `core/engine/*Adapter.cpp` được include header trong thư mục này.

## Hiện có

| Thư mục | Nguồn | Trạng thái |
|---|---|---|
| `engine-openkey/` | github.com/tuyenvm/OpenKey (GPL-3.0) | đã lấy `89c2fd3`, 1 patch, 28/28 conformance |
