#pragma once

#include <string>

#include <tl/expected.hpp>

namespace lankey::core::model {

// Error value carried by lk::expected in core. Codes are coarse on purpose: callers branch
// on them, humans read the message. Never put user text (typed content) into `message`.
struct Error {
    enum class Code {
        Unknown,
        InvalidArgument,
        NotFound,
        Io,          // file/database read or write failed
        Corrupted,   // data exists but cannot be parsed
        Unsupported, // feature missing on this platform/build
    };

    Code code = Code::Unknown;
    std::string message;

    static Error make(Code code, std::string message) { return Error{code, std::move(message)}; }
};

} // namespace lankey::core::model

// tl::expected instead of std::expected keeps core on C++20 (std::expected needs
// /std:c++23 on MSVC). Everything in LanKey spells it lk::expected so the switch later is
// a one-line change here.
namespace lk {
template <class T>
using expected = tl::expected<T, lankey::core::model::Error>;
using unexpected = tl::unexpected<lankey::core::model::Error>;
} // namespace lk
