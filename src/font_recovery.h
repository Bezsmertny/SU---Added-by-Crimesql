#pragma once
#include "windows_features.h"

namespace nh {
// Explicit locations let integration tests exercise the real transaction on disposable keys.
struct FontRecoveryTarget {
    HKEY machine_root;
    std::wstring machine_base;
    HKEY user_root;
    std::wstring user_base;
    std::wstring metrics_key;
    std::wstring fonts_directory;
    std::wstring backup_file;
    UINT dpi = 96;
};
Result restore_fonts_at(const FontRecoveryTarget& target);
std::wstring current_user_sid();
Result restore_system_fonts(const std::wstring& user_sid);
}
