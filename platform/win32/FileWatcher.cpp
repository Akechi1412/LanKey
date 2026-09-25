#include "platform/win32/FileWatcher.h"

#include <utility>
#include <vector>

namespace lankey::platform::win32 {

namespace {

constexpr DWORD kBufferBytes = 8 * 1024; // plenty for one directory's names

bool sameName(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

} // namespace

FileWatcher::~FileWatcher() {
    stop();
}

bool FileWatcher::start(const std::filesystem::path& directory, std::wstring fileName,
                        std::function<void()> onChanged) {
    stop();
    fileName_ = std::move(fileName);
    onChanged_ = std::move(onChanged);
    stopping_.store(false);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ == nullptr) return false;
    thread_ = std::thread([this, dir = directory.wstring()] { threadMain(dir); });
    return true;
}

void FileWatcher::stop() {
    if (!thread_.joinable()) {
        if (stopEvent_ != nullptr) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
        return;
    }
    stopping_.store(true);
    SetEvent(stopEvent_);
    thread_.join();
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

void FileWatcher::threadMain(std::wstring directory) {
    const HANDLE dir =
        CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dir == INVALID_HANDLE_VALUE) return;

    const HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ioEvent == nullptr) {
        CloseHandle(dir);
        return;
    }
    std::vector<std::byte> buffer(kBufferBytes);

    while (!stopping_.load()) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = ioEvent;
        ResetEvent(ioEvent);
        constexpr DWORD kFilter =
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE;
        if (ReadDirectoryChangesW(dir, buffer.data(), static_cast<DWORD>(buffer.size()),
                                  /*watchSubtree=*/FALSE, kFilter, nullptr, &overlapped,
                                  nullptr) == 0) {
            break;
        }

        const HANDLE waits[] = {ioEvent, stopEvent_};
        const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (which != WAIT_OBJECT_0) {
            CancelIoEx(dir, &overlapped);
            DWORD ignored = 0;
            GetOverlappedResult(dir, &overlapped, &ignored, TRUE);
            break;
        }

        DWORD bytes = 0;
        if (GetOverlappedResult(dir, &overlapped, &bytes, FALSE) == 0) break;
        if (bytes == 0) continue; // the buffer overflowed: the owner re-reads anyway

        bool hit = false;
        const std::byte* at = buffer.data();
        while (true) {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(at);
            const std::wstring_view name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (sameName(name, fileName_)) hit = true;
            if (info->NextEntryOffset == 0) break;
            at += info->NextEntryOffset;
        }
        if (hit && onChanged_ && !stopping_.load()) onChanged_();
    }

    CloseHandle(ioEvent);
    CloseHandle(dir);
}

} // namespace lankey::platform::win32
