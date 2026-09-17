#pragma once

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace lankey::app {

// Minimal event log: one line per event with a timestamp, appended to lankey.log in the
// data directory. NEVER log key content, syllables or phrases - only events and numbers.
// (spdlog with the same rule replaces this once logging needs levels/rotation.)
//
// Threading: any thread; a mutex serialises writes.
class Log {
public:
    static Log& instance();

    void open(const std::filesystem::path& file);
    void close();
    void write(std::string_view line);

private:
    std::mutex mutex_;
    std::FILE* file_ = nullptr;
};

// Convenience: LOG("hook started, thread=%lu", id)
void logf(const char* format, ...);

} // namespace lankey::app
