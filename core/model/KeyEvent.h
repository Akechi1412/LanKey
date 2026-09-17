#pragma once

#include <cstdint>

namespace lankey::core::model {

// Platform-independent key code. Values match Windows VK codes for letters/digits so the
// win32 adapter can map 1:1, but this must NOT be treated as a Win32 type - core knows
// nothing about Win32. Only keys the pipeline needs to distinguish are listed; the actual
// character lives in KeyEvent::unicode.
enum class VirtualKey : std::uint16_t {
    Unknown = 0x00,

    Backspace = 0x08,
    Tab = 0x09,
    Enter = 0x0D,
    Shift = 0x10, // generic modifier keys, reported so hotkey chords can be detected
    Control = 0x11,
    Alt = 0x12,
    Escape = 0x1B,
    Space = 0x20,

    End = 0x23,
    Home = 0x24,
    ArrowLeft = 0x25,
    ArrowUp = 0x26,
    ArrowRight = 0x27,
    ArrowDown = 0x28,
    Delete = 0x2E,

    Digit0 = 0x30,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,

    A = 0x41,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    // Punctuation/symbol keys: the pipeline only needs to know "printable, not a letter".
    // The specific character is read from KeyEvent::unicode.
    Punctuation = 0xE0,
};

enum class Modifier : std::uint8_t {
    None = 0,
    Shift = 1 << 0,
    Control = 1 << 1,
    Alt = 1 << 2,
    Win = 1 << 3,
    CapsLock = 1 << 4,
};

constexpr Modifier operator|(Modifier a, Modifier b) noexcept {
    return static_cast<Modifier>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr bool has(Modifier set, Modifier flag) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(flag)) != 0;
}

// A keyboard event normalized by the platform layer.
// Value type, immutable once created; passes through the whole pipeline unchanged.
struct KeyEvent {
    VirtualKey key = VirtualKey::Unknown;
    // The character this key produces under the current layout/modifiers (Shift/CapsLock
    // already applied), or 0 for control keys. Filled by the platform via ToUnicodeEx on a
    // COPY of the keyboard state (ToUnicode mutates the kernel's dead-key state).
    char32_t unicode = 0;
    Modifier modifiers = Modifier::None;
    bool isDown = true;
    std::int64_t timestampMs = 0;
    // true if LanKey itself sent this key (SendInput with dwExtraInfo = kLanKeyMagic).
    // The pipeline must skip it so it never processes its own output -> infinite loop.
    bool injectedBySelf = false;

    [[nodiscard]] constexpr bool isLetter() const noexcept {
        return key >= VirtualKey::A && key <= VirtualKey::Z;
    }
    [[nodiscard]] constexpr bool isDigit() const noexcept {
        return key >= VirtualKey::Digit0 && key <= VirtualKey::Digit9;
    }
    [[nodiscard]] constexpr bool hasSystemModifier() const noexcept {
        return has(modifiers, Modifier::Control) || has(modifiers, Modifier::Alt) ||
               has(modifiers, Modifier::Win);
    }
};

} // namespace lankey::core::model
