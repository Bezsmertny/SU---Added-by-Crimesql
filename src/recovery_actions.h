#pragma once
#include <string>

namespace nh {
enum class RecoveryAction {
    defender_ui,defender_quick,defender_full,defender_offline,
    sfc_verify,sfc_repair,dism_restore,restore_points,
    powershell_admin,cmd_admin,winre_info,advanced_startup,
    startup_audit,startup_settings,system_info,diagnostic_report
};
struct ActionInfo {
    const wchar_t* title;
    const wchar_t* description;
    const wchar_t* confirmation;
};
const ActionInfo& action_info(RecoveryAction action);
std::wstring powershell_quote(const std::wstring& value);
std::wstring recovery_script(RecoveryAction action,const std::wstring& system_directory);
std::wstring maintenance_wrapper(const std::wstring& script,const std::wstring& transcript);
}
