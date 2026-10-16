#pragma once
#include "recovery_features.h"
#include "windows_system.h"

namespace nh {
struct Settings {
    std::array<Binding, 3> bindings{{{MOD_CONTROL | MOD_ALT, VK_F8}, {MOD_CONTROL | MOD_ALT, VK_F9}, {MOD_CONTROL | MOD_ALT, VK_F10}}};
    bool animations = true;
    bool high_priority = false;
};
struct Result { bool success; std::wstring message; };
std::wstring data_directory();
std::wstring executable_path();
Settings load_settings();
Result save_settings(const Settings& settings);
bool autorun_enabled();
Result set_autorun(bool enabled);
Result set_high_priority(bool enabled);
Result recover_mouse();
Result recover_keyboard();
Result start_defender_scan(HWND owner, HANDLE& process);
bool sandbox_available();
struct LabResult { bool success; std::wstring message; std::wstring hash; };
LabResult launch_sandbox(HWND owner, const std::wstring& sample);
bool write_unicode_file(const std::wstring& path, const std::wstring& contents, bool exclusive = false);
std::wstring binding_name(Binding binding);
void apply_window_theme(HWND window);
}
