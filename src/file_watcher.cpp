#include "file_watcher.h"
#include <cstring>
#include <vector>

namespace nh {
std::vector<FileEvent> decode_notifications(const void* raw, std::size_t size, RenameTracker& tracker) {
    const auto* data = static_cast<const unsigned char*>(raw);
    std::vector<FileEvent> out;
    std::size_t offset = 0;
    while (offset < size) {
        DWORD next = 0, action = 0, length = 0;
        if (size - offset < 12) break;
        std::memcpy(&next, data + offset, 4);
        std::memcpy(&action, data + offset + 4, 4);
        std::memcpy(&length, data + offset + 8, 4);
        if (length == 0 || length % sizeof(wchar_t) != 0 || length > size - offset - 12 ||
            (next && (next < 12 + length || next > size - offset))) break;
        std::wstring name(length / sizeof(wchar_t), L'\0');
        std::memcpy(name.data(), data + offset + 12, length);
        if (name.find(L'\0') != std::wstring::npos) break;
        auto events = tracker.accept(action, name);
        out.insert(out.end(), events.begin(), events.end());
        if (!next) return out;
        offset += next;
    }
    tracker.flush();
    out.push_back({FileEvent::Kind::lost, L"Повреждённый или неполный пакет файловых событий", {}});
    return out;
}

void FileWatcher::stop() {
    if (stop_event) SetEvent(stop_event);
    if (directory != INVALID_HANDLE_VALUE) CancelIoEx(directory, nullptr);
    if (worker.joinable()) worker.join();
    if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
    if (stop_event) CloseHandle(stop_event);
    if (ready_event) CloseHandle(ready_event);
    directory = INVALID_HANDLE_VALUE; stop_event = ready_event = nullptr;
    armed = false;
}
bool FileWatcher::start(const std::wstring& root, std::function<void(FileEvent)> callback, DWORD& error) {
    stop(); failure = 0;
    directory = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ready_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event || !ready_event) { error = GetLastError(); stop(); return false; }
    try {
        worker = std::thread([this, callback = std::move(callback)] {
            HANDLE changed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!changed) { failure = GetLastError(); SetEvent(ready_event); return; }
            std::vector<unsigned char> buffer(64 * 1024);
            RenameTracker tracker;
            while (WaitForSingleObject(stop_event, 0) == WAIT_TIMEOUT) {
                OVERLAPPED overlap{}; overlap.hEvent = changed;
                ResetEvent(changed);
                if (!ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                    nullptr, &overlap, nullptr)) {
                    failure = GetLastError(); SetEvent(ready_event);
                    callback({FileEvent::Kind::lost, L"Наблюдение остановлено. Код ошибки: " + std::to_wstring(failure.load()), {}});
                    break;
                }
                armed = true; SetEvent(ready_event);
                HANDLE events[]{stop_event, changed};
                const DWORD result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (result != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(directory, &overlap);
                    DWORD ignored = 0; GetOverlappedResult(directory, &overlap, &ignored, TRUE);
                    break;
                }
                DWORD received = 0;
                if (!GetOverlappedResult(directory, &overlap, &received, FALSE)) {
                    const auto code = GetLastError();
                    if (code == ERROR_OPERATION_ABORTED) break;
                    tracker.flush();
                    callback({FileEvent::Kind::lost, L"Не удалось прочитать события. Код: " + std::to_wstring(code), {}});
                    if (code != ERROR_NOTIFY_ENUM_DIR) break;
                    continue;
                }
                if (!received) {
                    tracker.flush();
                    callback({FileEvent::Kind::lost, L"Буфер переполнен: часть событий потеряна", {}});
                } else {
                    for (auto& event : decode_notifications(buffer.data(), received, tracker)) callback(std::move(event));
                }
            }
            for (auto& event : tracker.flush()) callback(std::move(event));
            CloseHandle(changed);
        });
    } catch (...) { error = ERROR_NOT_ENOUGH_MEMORY; stop(); return false; }
    const auto waited = WaitForSingleObject(ready_event, 5000);
    if (waited != WAIT_OBJECT_0 || !armed) {
        error = failure ? failure.load() : ERROR_TIMEOUT; stop(); return false;
    }
    error = ERROR_SUCCESS;
    return true;
}
}
