#include "core/engine/OpenKeyEngineAdapter.h"

#include <atomic>
#include <cwctype>
#include <stdexcept>

// Upstream headers. OPENKEY_PORTABLE_KEYCODES (set by the openkey_engine target) selects
// platforms/linux.h, so KEY_* below are X11 keycodes - they never leave this file.
#include "Engine.h"

// OpenKey expects the host application to define its settings as globals - this is the
// one place in LanKey where mutable globals are unavoidable, and the adapter is the only
// code that touches them. Everything not exposed through EngineSettings is pinned to "off".
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
int vLanguage = 1; // 1 = Vietnamese
int vInputType = vTelex;
int vFreeMark = 0;
int vCodeTable = 0; // Unicode
int vSwitchKeyStatus = 0;
int vCheckSpelling = 1;
int vUseModernOrthography = 1;
int vQuickTelex = 0;
int vRestoreIfWrongSpelling = 1;
int vFixRecommendBrowser = 0;
int vUseMacro = 0;
int vUseMacroInEnglishMode = 0;
int vAutoCapsMacro = 0;
int vUseSmartSwitchKey = 0;
int vUpperCaseFirstChar = 0;
int vTempOffSpelling = 0;
int vAllowConsonantZFWJ = 0;
int vQuickStartConsonant = 0;
int vQuickEndConsonant = 0;
int vRememberCode = 0;
int vOtherLanguage = 0;
int vTempOffOpenKey = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

namespace lankey::core::engine {

using model::EngineResult;
using model::EngineSettings;
using model::InputMethod;
using model::KeyEvent;
using model::Modifier;
using model::VirtualKey;

namespace {

// The engine result struct and the single-instance guard. Global because the engine is.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> instanceAlive{false};
vKeyHookState* hookState = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Map our platform-neutral VirtualKey onto the keycode table the engine was compiled
// with. Returns KEY_EMPTY for keys the engine has no notion of.
Uint16 toUpstreamKey(const KeyEvent& key) {
    switch (key.key) {
    case VirtualKey::Backspace:
        return KEY_DELETE; // OpenKey's name for Backspace
    case VirtualKey::Tab:
        return KEY_TAB;
    case VirtualKey::Enter:
        return KEY_ENTER;
    case VirtualKey::Escape:
        return KEY_ESC;
    case VirtualKey::Space:
        return KEY_SPACE;
    case VirtualKey::ArrowLeft:
        return KEY_LEFT;
    case VirtualKey::ArrowRight:
        return KEY_RIGHT;
    case VirtualKey::ArrowUp:
        return KEY_UP;
    case VirtualKey::ArrowDown:
        return KEY_DOWN;
    default:
        break;
    }
    if (key.isLetter()) {
        static constexpr Uint16 kLetters[26] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
            KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
            KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
        };
        return kLetters[static_cast<int>(key.key) - static_cast<int>(VirtualKey::A)];
    }
    if (key.isDigit()) {
        static constexpr Uint16 kDigits[10] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
                                               KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
        return kDigits[static_cast<int>(key.key) - static_cast<int>(VirtualKey::Digit0)];
    }
    if (key.key == VirtualKey::Punctuation) {
        switch (key.unicode) {
        case U'.':
            return KEY_DOT;
        case U',':
            return KEY_COMMA;
        case U';':
            return KEY_SEMICOLON;
        case U'\'':
            return KEY_QUOTE;
        case U'/':
            return KEY_SLASH;
        case U'\\':
            return KEY_BACK_SLASH;
        case U'-':
            return KEY_MINUS;
        case U'=':
            return KEY_EQUALS;
        case U'`':
            return KEY_BACKQUOTE;
        case U'[':
            return KEY_LEFT_BRACKET;
        case U']':
            return KEY_RIGHT_BRACKET;
        default:
            return KEY_EMPTY;
        }
    }
    return KEY_EMPTY;
}

// Decode one charData[] entry the way the upstream win32 host does in SendNewCharString().
char32_t decodeChar(Uint32 data) {
    if ((data & PURE_CHARACTER_MASK) != 0) {
        return static_cast<char32_t>(data & CHAR_MASK);
    }
    if ((data & CHAR_CODE_MASK) == 0) {
        // Still a keycode (possibly with CAPS_MASK): a plain letter that was retyped.
        return static_cast<char32_t>(keyCodeToCharacter(data));
    }
    // vCodeTable == 0 (Unicode): the low 16 bits are the code point.
    return static_cast<char32_t>(data & CHAR_MASK);
}

Uint8 capsStatus(const KeyEvent& key) {
    const bool shift = has(key.modifiers, Modifier::Shift);
    const bool caps = has(key.modifiers, Modifier::CapsLock);
    if (shift && caps) return 0;
    if (shift) return 1;
    if (caps) return 2;
    return 0;
}

} // namespace

OpenKeyEngineAdapter::OpenKeyEngineAdapter() {
    if (instanceAlive.exchange(true)) {
        throw std::logic_error("OpenKeyEngineAdapter: engine state is global, one instance only");
    }
    hookState = static_cast<vKeyHookState*>(vKeyInit());
    applySettingsToGlobals();
}

OpenKeyEngineAdapter::~OpenKeyEngineAdapter() {
    hookState = nullptr;
    instanceAlive = false;
}

void OpenKeyEngineAdapter::applySettingsToGlobals() const {
    switch (settings_.inputMethod) {
    case InputMethod::Telex:
        vInputType = vTelex;
        break;
    case InputMethod::Vni:
        vInputType = vVNI;
        break;
    case InputMethod::SimpleTelex:
        vInputType = vSimpleTelex1;
        break;
    }
    vUseModernOrthography = settings_.modernToneMark ? 1 : 0;
    vCheckSpelling = settings_.spellCheck ? 1 : 0;
    vRestoreIfWrongSpelling = settings_.spellCheck ? 1 : 0;
    vQuickTelex = settings_.quickTelex ? 1 : 0;
}

void OpenKeyEngineAdapter::configure(const EngineSettings& settings) {
    settings_ = settings;
    applySettingsToGlobals();
    // vKeyInit() re-reads vCheckSpelling into the engine's private copy.
    hookState = static_cast<vKeyHookState*>(vKeyInit());
    onScreen_.clear();
    transformApplied_ = false;
}

void OpenKeyEngineAdapter::reset() {
    startNewSession();
    onScreen_.clear();
    transformApplied_ = false;
}

EngineResult OpenKeyEngineAdapter::passThrough() const {
    EngineResult r;
    r.action = EngineResult::Action::PassThrough;
    r.composed.text = onScreen_;
    r.composed.vietnameseTransformApplied = transformApplied_;
    return r;
}

EngineResult OpenKeyEngineAdapter::process(const KeyEvent& key) {
    if (key.injectedBySelf || !key.isDown) {
        return passThrough();
    }
    if (key.hasSystemModifier()) {
        reset();
        return passThrough();
    }

    const Uint16 upstreamKey = toUpstreamKey(key);
    if (upstreamKey == KEY_EMPTY) {
        // Unknown to the engine: treat as a word break so the next syllable starts clean.
        reset();
        return passThrough();
    }

    vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, upstreamKey, capsStatus(key),
                    /*otherControlKey=*/false);

    const auto code = static_cast<HoolCodeState>(hookState->code);
    if (code == vDoNothing || code == vBreakWord || code == vReplaceMaro) {
        // vReplaceMaro cannot happen (macros are off) but must not fall through either.
        return trackPassThrough(key);
    }
    return buildReplacement(key, code);
}

// The engine let the key through: mirror what the application will show.
EngineResult OpenKeyEngineAdapter::trackPassThrough(const KeyEvent& key) {
    if (key.key == VirtualKey::Backspace) {
        if (!onScreen_.empty()) onScreen_.pop_back();
    } else if (key.isLetter() || key.isDigit()) {
        onScreen_.push_back(key.unicode);
    } else {
        // Space, punctuation, navigation: the syllable is over.
        onScreen_.clear();
        transformApplied_ = false;
    }
    return passThrough();
}

// vWillProcess / vRestore / vRestoreAndStartNewSession: translate the global hook state.
EngineResult OpenKeyEngineAdapter::buildReplacement(const KeyEvent& key, int code) {
    EngineResult r;
    r.action = EngineResult::Action::Replace;
    r.deleteCount = hookState->backspaceCount;
    // charData[] is filled back-to-front: index newCharCount-1 is the first character.
    for (int i = static_cast<int>(hookState->newCharCount) - 1; i >= 0; --i) {
        const char32_t ch = decodeChar(hookState->charData[i]);
        if (ch != 0) r.insert.push_back(ch);
    }

    const bool restore = (code == vRestore || code == vRestoreAndStartNewSession);
    if (restore) {
        // The engine gave up on the syllable (invalid spelling) and asks the host to
        // retype the raw keys; the triggering key itself is not in charData.
        if (key.unicode != 0) r.insert.push_back(key.unicode);
    }
    transformApplied_ = !restore;

    // Keep the screen mirror in sync.
    const auto del = static_cast<std::size_t>(r.deleteCount);
    onScreen_.erase(onScreen_.size() - (del > onScreen_.size() ? onScreen_.size() : del));
    onScreen_ += r.insert;

    if (restore) {
        // Restored syllables end at a break key (space, punctuation) - start fresh.
        if (!key.isLetter() && !key.isDigit()) {
            onScreen_.clear();
        }
        if (code == vRestoreAndStartNewSession) {
            startNewSession();
        }
    }

    r.composed.text = onScreen_;
    r.composed.vietnameseTransformApplied = transformApplied_;
    return r;
}

} // namespace lankey::core::engine
