# engine-openkey

| | |
|---|---|
| Upstream | https://github.com/tuyenvm/OpenKey |
| Commit | `89c2fd3bf258562f2349f89b49d81e2f140c3fc3` (2026-06-15) |
| Taken on | 2026-09-15 |
| License | GPL-3.0 (see `LICENSE`, copied verbatim from upstream root) |
| Subset | `Sources/OpenKey/engine/` only (no UI, no platform layer) |

## What is here

`src/` is a verbatim copy of `Sources/OpenKey/engine/` plus the patches in `patches/`.

## Patches

| # | File | Why |
|---|---|---|
| 001 | `DataType.h`, `Engine.cpp` | `OPENKEY_PORTABLE_KEYCODES` selects the platform-neutral key table (`platforms/linux.h`) without pulling in `<windows.h>`. Upstream `DataType.h` does `using namespace std;` before including `<windows.h>`, which makes `byte` ambiguous under C++17+ and, more importantly, ties the engine to Win32. |
| 002 | `DataType.h`, `Engine.cpp` | User-defined input method: a fifth `vInputType` (`vCustom`) with its own key table: `CUSTOM_FUNCTION_COUNT` (13) functions - the eleven `ProcessingChar` positions plus standalone ơ and ư (Shift for capitals) - each with up to `CUSTOM_KEYS_PER_FUNCTION` (4) key codes, set with `vSetCustomKeys()`. The key tests (`IS_KEY_*`, `IS_MARK_KEY`) and the đ-after-consonant rule read that table instead of hard-coding the Telex letters; the â/ô/ê/horn keys map onto the Telex letters the vowel tables are keyed by (a shared key, VNI-style `6` for â and ô, is resolved by the last vowel typed); a punctuation key given a function is not a word break; the Telex `[`/`]` standalone-letter shortcut is replaced by the two standalone functions, and the horn key typed alone gives ư like Telex `w`. |

## Known upstream traits the adapter must hide

- All engine state is global (`static` in `Engine.cpp`, `extern int v*` settings). Only one
  engine instance can exist per process.
- Settings are `extern int` globals that the host application must define.
- `vKeyHandleEvent()` writes its result into a global `vKeyHookState` returned by `vKeyInit()`.
- `charData[]` is filled in reverse order (index `newCharCount-1` is the first character).
- Key codes are platform-specific macros (`KEY_A` ...); with patch 001 these are the X11
  keycodes from `platforms/linux.h`.

## Updating

1. Replace `src/` with the new upstream `Sources/OpenKey/engine/`.
2. `git apply patches/*.patch` (from this directory).
3. Build with `-DLANKEY_ENGINE_OPENKEY=ON` and run `lankey_tests`.
