# Security

LanKey is, technically, a keylogger: a low-level keyboard hook sees every key you press.
What separates it from malware is transparent design and deliberate restraint. This file
says what that means concretely and how to report a problem.

## Reporting a vulnerability

Please do **not** open a public issue for security problems. E-mail
**nguyenphong10042002@gmail.com** with a description and, if you can, steps to reproduce.
You will get an acknowledgement within 7 days. Once a fix is released the report is
credited in the release notes unless you prefer otherwise.

## What LanKey does with keystrokes

- Keys are processed in memory to compose Vietnamese; the composed syllable is then
  forgotten. Only **phrases of 1–5 syllables plus their counts and timestamps** are kept,
  never raw keys, sentences, or anything beyond a 5-syllable window.
- Nothing is recorded from password fields (UI Automation `IsPassword`), from password
  managers, remote-desktop and terminal windows (default list in
  `core/smart/privacy/PrivacyFilter.cpp`, user-extendable), from strings containing digits
  or symbols, from e-mail addresses, card and ID numbers, 20+ character tokens or
  high-entropy strings (`ContentHeuristicRule`).
- The learned data lives in `%APPDATA%\LanKey\user_lexicon.enc`, an image of an
  in-memory SQLite database sealed with Windows DPAPI (user scope, extra entropy). It can
  only be opened by the same Windows account on the same machine; there is no journal or
  temp copy of the plaintext on disk. `SecureZeroMemory` wipes plaintext buffers.
- The log file contains timings, counters and error messages only - never typed content.
  This is enforced by review: `logf` calls are the only logging path.
- **No network code.** The binary imports no networking DLL;
  `tools/audit/check-no-network.ps1` verifies that on every CI build. No update check, no
  telemetry, no crash reporting.
- The full user-facing statement is [DATA-POLICY.md](DATA-POLICY.md); `data_policy_test.cpp`
  pins its concrete claims to the code.

## What LanKey never does

No packing, obfuscation, DLL injection, hidden auto-start, anti-debugging, or downloading
of code. The hook is `WH_KEYBOARD_LL` installed by the visible tray process; quitting the
tray icon removes it.

## Not yet done (tracked for the public beta)

- Code signing of releases and Microsoft/SmartScreen submission.
- Reproducible builds.
- Optional master-password protection of the lexicon (DPAPI is the default and only
  scheme today).

## Supported versions

Only the latest release is supported.
