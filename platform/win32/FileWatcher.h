#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include "platform/win32/Win32.h"

namespace lankey::platform::win32 {

// Tells the owner when one file in a directory is written by someone else (an editor
// saving settings.json). Watches the directory rather than the file: editors save by
// writing a temporary file and renaming it over the original, so a handle to the file
// itself would stop seeing changes after the first save.
//
// The callback runs on the watcher thread and must do nothing but hand the news to the UI
// thread (PostMessage). Coalescing and reading the file are the owner's job: one save
// produces several notifications.
class FileWatcher {
public:
    FileWatcher() = default;
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // `fileName` is compared case-insensitively against the name the OS reports.
    bool start(const std::filesystem::path& directory, std::wstring fileName,
               std::function<void()> onChanged);
    void stop();
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }

private:
    void threadMain(std::wstring directory);

    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    std::wstring fileName_;
    std::function<void()> onChanged_;
    std::atomic<bool> stopping_{false};
};

} // namespace lankey::platform::win32
