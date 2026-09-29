#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/interfaces/ISnippetSource.h"
#include "core/model/AtomicSnapshot.h"
#include "core/snippet/SnippetIndex.h"

namespace lankey::app {

// The hook thread's door to snippets.json.
//
// Everything it needs is handed to it from the UI thread as an immutable snapshot: the
// index, and the clipboard text if any snippet asks for one. Nothing in here opens a file,
// takes a lock or calls into Win32 - this runs inside the keyboard hook, where a call that
// waits is a keystroke the user watches arrive late.
class SnippetSource final : public core::ISnippetSource {
public:
    // --- hook thread ---
    [[nodiscard]] std::optional<core::SnippetMatch>
    match(std::u32string_view rawWord) const override;
    [[nodiscard]] std::optional<core::snippet::Rendered>
    expand(std::u32string_view abbr) const override;

    // --- UI thread ---
    void publish(std::shared_ptr<const core::snippet::SnippetIndex> index) {
        index_.store(std::move(index));
    }
    void setClipboard(std::u32string text) {
        clipboard_.store(std::make_shared<const std::u32string>(std::move(text)));
    }
    [[nodiscard]] std::shared_ptr<const core::snippet::SnippetIndex> index() const {
        return index_.load();
    }

private:
    core::model::AtomicSnapshot<core::snippet::SnippetIndex> index_;
    core::model::AtomicSnapshot<std::u32string> clipboard_;
};

} // namespace lankey::app
