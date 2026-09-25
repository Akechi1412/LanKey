#include "core/engine/OpenKeyEngineAdapter.h"

#include <algorithm>
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

using model::CodeTable;
using model::EngineResult;
using model::EngineSettings;
using model::InputMethod;
using model::kCustomKeyCount;
using model::kDefaultCustomKeys;
using model::KeyEvent;
using model::Modifier;
using model::VirtualKey;

namespace {

// The engine result struct and the single-instance guard. Global because the engine is.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> instanceAlive{false};
vKeyHookState* hookState = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

constexpr Uint16 kLetterKeys[26] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
};
constexpr Uint16 kDigitKeys[10] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
                                   KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};

// Engine keycode for an unshifted US-layout character (the custom-method key set).
Uint16 keyCodeForChar(char c) {
    if (c >= 'a' && c <= 'z') return kLetterKeys[c - 'a'];
    if (c >= '0' && c <= '9') return kDigitKeys[c - '0'];
    switch (c) {
    case '.':
        return KEY_DOT;
    case ';':
        return KEY_SEMICOLON;
    case '\'':
        return KEY_QUOTE;
    case '/':
        return KEY_SLASH;
    case '\\':
        return KEY_BACK_SLASH;
    case '-':
        return KEY_MINUS;
    case '=':
        return KEY_EQUALS;
    case '`':
        return KEY_BACKQUOTE;
    case '[':
        return KEY_LEFT_BRACKET;
    case ']':
        return KEY_RIGHT_BRACKET;
    default:
        return KEY_EMPTY;
    }
}

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
        return kLetterKeys[static_cast<int>(key.key) - static_cast<int>(VirtualKey::A)];
    }
    if (key.isDigit()) {
        return kDigitKeys[static_cast<int>(key.key) - static_cast<int>(VirtualKey::Digit0)];
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
        case U'{': // Shift+[ : the engine sees the bracket with caps set (standalone Ơ)
            return KEY_LEFT_BRACKET;
        case U']':
        case U'}':
            return KEY_RIGHT_BRACKET;
        default:
            return KEY_EMPTY;
        }
    }
    return KEY_EMPTY;
}

// Decode one charData[] entry the way the upstream win32 host does in SendNewCharString()
// and append it to `out`. Returns how many code units went on screen: 1, or 2 for the
// "double" tables (VNI Windows, Unicode compound) where a marked letter is the base letter
// followed by a mark character - the count Backspace bookkeeping needs.
int decodeInto(std::u32string& out, Uint32 data) {
    if ((data & PURE_CHARACTER_MASK) != 0) {
        out.push_back(static_cast<char32_t>(data & CHAR_MASK));
        return 1;
    }
    if ((data & CHAR_CODE_MASK) == 0) {
        // Still a keycode (possibly with CAPS_MASK): a plain letter that was retyped.
        out.push_back(static_cast<char32_t>(keyCodeToCharacter(data)));
        return 1;
    }
    const Uint32 value = data & CHAR_MASK;
    switch (vCodeTable) {
    case 2: { // VNI Windows: low byte = base letter, high byte = VNI-font mark glyph
        const auto high = static_cast<char32_t>((value >> 8) & 0xFF);
        if (high <= 32) { // same rule as upstream ConvertTool: a low high byte is a single glyph
            out.push_back(static_cast<char32_t>(value));
            return 1;
        }
        out.push_back(static_cast<char32_t>(value & 0xFF));
        out.push_back(high);
        return 2;
    }
    case 3: { // Unicode compound: bits 13-15 = tone index, bits 0-12 = precomposed base
        static constexpr char32_t kMarks[] = {0x0301, 0x0300, 0x0309, 0x0303, 0x0323};
        const Uint32 mark = value >> 13;
        if (mark == 0 || mark > 5) {
            out.push_back(static_cast<char32_t>(value));
            return 1;
        }
        out.push_back(static_cast<char32_t>(value & 0x1FFF));
        out.push_back(kMarks[mark - 1]); // sắc huyền hỏi ngã nặng
        return 2;
    }
    default: // Unicode (code point) and TCVN3 (one byte, glyph at that ANSI position)
        out.push_back(static_cast<char32_t>(value));
        return 1;
    }
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
    case InputMethod::Custom: {
        static_assert(kCustomKeyCount == CUSTOM_FUNCTION_COUNT);
        Uint16 keys[kCustomKeyCount][CUSTOM_KEYS_PER_FUNCTION];
        auto table = model::parseCustomKeys(settings_.customKeys);
        if (!table) table = model::parseCustomKeys(kDefaultCustomKeys);
        for (int i = 0; i < kCustomKeyCount; ++i) {
            const std::string& group = (*table)[static_cast<std::size_t>(i)];
            for (int j = 0; j < CUSTOM_KEYS_PER_FUNCTION; ++j) {
                keys[i][j] = j < static_cast<int>(group.size())
                                 ? keyCodeForChar(group[static_cast<std::size_t>(j)])
                                 : KEY_EMPTY;
            }
        }
        vSetCustomKeys(keys);
        vInputType = vCustom;
        break;
    }
    }
    switch (settings_.codeTable) {
    case CodeTable::Unicode:
        vCodeTable = 0;
        break;
    case CodeTable::Tcvn3:
        vCodeTable = 1;
        break;
    case CodeTable::VniWindows:
        vCodeTable = 2;
        break;
    case CodeTable::UnicodeCompound:
        vCodeTable = 3;
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
    widths_.clear();
    transformApplied_ = false;
}

void OpenKeyEngineAdapter::reset() {
    startNewSession();
    // startNewSession() only ends the syllable; the engine also keeps a stack of finished
    // words so Backspace can walk back into the previous one. reset() means "the text you
    // remember is gone" (focus moved, a paragraph ended, or LanKey itself rewrote the
    // screen), and a stale stack there restores raw keys over text that no longer exists.
    hookState = static_cast<vKeyHookState*>(vKeyInit());
    onScreen_.clear();
    widths_.clear();
    transformApplied_ = false;
}

EngineResult OpenKeyEngineAdapter::passThrough() const {
    EngineResult r;
    r.action = EngineResult::Action::PassThrough;
    // Legacy code tables put font code points on screen, not Vietnamese letters: report no
    // composition so the smart layer (learning, suggestions, AutoCorrect) stays out of it.
    if (vCodeTable == 0) {
        r.composed.text = onScreen_;
        r.composed.vietnameseTransformApplied = transformApplied_;
    }
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
        if (!widths_.empty()) {
            const std::size_t w = widths_.back();
            widths_.pop_back();
            onScreen_.erase(onScreen_.size() - (std::min)(w, onScreen_.size()));
            // Nothing of the syllable is left: whatever the engine did to it is gone with
            // it, so the next word must not inherit "a Vietnamese transform was applied"
            // (that flag is what keeps AutoCorrect away from English).
            if (onScreen_.empty()) transformApplied_ = false;
            if (w > 1) {
                // Double table: the marked letter is two code units on screen, one Backspace
                // would leave the base letter behind. Swallow the key and delete both.
                EngineResult r = passThrough();
                r.action = EngineResult::Action::Replace;
                r.deleteCount = static_cast<int>(w);
                return r;
            }
        }
    } else if (key.isLetter() || key.isDigit()) {
        onScreen_.push_back(key.unicode);
        widths_.push_back(1);
    } else {
        // Space, punctuation, navigation: the syllable is over.
        onScreen_.clear();
        widths_.clear();
        transformApplied_ = false;
    }
    return passThrough();
}

// vWillProcess / vRestore / vRestoreAndStartNewSession: translate the global hook state.
EngineResult OpenKeyEngineAdapter::buildReplacement(const KeyEvent& key, int code) {
    EngineResult r;
    r.action = EngineResult::Action::Replace;
    // backspaceCount is in engine characters; on screen a marked letter may be two code
    // units (double tables). Sum the widths of the characters being removed.
    int deleteUnits = 0;
    for (int i = 0; i < hookState->backspaceCount; ++i) {
        if (widths_.empty()) {
            ++deleteUnits; // beyond what we saw typed: assume one unit, like the host does
            continue;
        }
        deleteUnits += widths_.back();
        widths_.pop_back();
    }
    r.deleteCount = deleteUnits;
    // charData[] is filled back-to-front: index newCharCount-1 is the first character.
    for (int i = static_cast<int>(hookState->newCharCount) - 1; i >= 0; --i) {
        const std::size_t before = r.insert.size();
        const int width = decodeInto(r.insert, hookState->charData[i]);
        if (r.insert.size() > before && r.insert.back() == 0) {
            r.insert.resize(before); // a zero: nothing to show
            continue;
        }
        widths_.push_back(static_cast<std::uint8_t>(width));
    }

    const bool restore = (code == vRestore || code == vRestoreAndStartNewSession);
    if (restore) {
        // Capture the composed syllable before the mirror below overwrites it with the raw
        // keys: it is what AutoCorrect would need to see, and the only place it survives.
        r.restoredFrom = onScreen_;
    }
    if (restore) {
        // The engine gave up on the syllable (invalid spelling) and asks the host to
        // retype the raw keys; the triggering key itself is not in charData.
        if (key.unicode != 0) {
            r.insert.push_back(key.unicode);
            widths_.push_back(1);
        }
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
            widths_.clear();
        }
        if (code == vRestoreAndStartNewSession) {
            startNewSession();
        }
    }

    if (vCodeTable == 0) { // legacy tables: no Vietnamese text to learn from (see passThrough)
        r.composed.text = onScreen_;
        r.composed.vietnameseTransformApplied = transformApplied_;
    }
    return r;
}

} // namespace lankey::core::engine
