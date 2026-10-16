#pragma once
#include "recovery_features.h"
#include <windows.h>
#include <atomic>
#include <functional>
#include <thread>

namespace nh {
std::vector<FileEvent> decode_notifications(const void* data, std::size_t size, RenameTracker& tracker);
class FileWatcher {
    HANDLE directory = INVALID_HANDLE_VALUE, stop_event = nullptr, ready_event = nullptr;
    std::thread worker;
    std::atomic<bool> armed{false};
    std::atomic<DWORD> failure{0};
public:
    FileWatcher() = default;
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    ~FileWatcher() { stop(); }
    bool start(const std::wstring& root, std::function<void(FileEvent)> callback, DWORD& error);
    void stop();
};
}
