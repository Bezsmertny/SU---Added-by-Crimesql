#pragma once
#include "recovery.h"
#include <windows.h>
#include <vector>

namespace nh {
Observation read_policy(const Policy& policy);
struct RepairResult { bool success; std::wstring message; std::wstring backup_path{}; };
RepairResult restore_policy(const Policy& policy, const Observation& expected);
std::wstring system_summary();
std::wstring defender_status();
std::wstring error_text(DWORD code);
std::wstring backup_directory();
enum class Tool { defender, task_manager, system_info, recovery, backups };
bool open_tool(HWND owner, Tool tool, std::wstring& error);
}
