#include "app/Log.h"

#include <chrono>
#include <cstdarg>
#include <ctime>

namespace lankey::app {

Log& Log::instance() {
    static Log log;
    return log;
}

void Log::open(const std::filesystem::path& file) {
    const std::lock_guard lock(mutex_);
    if (file_ != nullptr) return;
    file_ = _wfopen(file.c_str(), L"ab");
}

void Log::close() {
    const std::lock_guard lock(mutex_);
    if (file_ != nullptr) std::fclose(file_);
    file_ = nullptr;
}

void Log::write(std::string_view line) {
    const std::lock_guard lock(mutex_);
    if (file_ == nullptr) return;
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::fprintf(file_, "%04d-%02d-%02d %02d:%02d:%02d.%03lld %.*s\n", tm.tm_year + 1900,
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                 static_cast<long long>(ms), static_cast<int>(line.size()), line.data());
    std::fflush(file_);
}

void logf(const char* format, ...) {
    char buffer[512];
    std::va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (n < 0) return;
    Log::instance().write(std::string_view(
        buffer, static_cast<std::size_t>((std::min)(n, static_cast<int>(sizeof(buffer) - 1)))));
}

} // namespace lankey::app
