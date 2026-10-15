#include "recovery.h"
#include <iomanip>
#include <sstream>

namespace nh {
const std::array<Policy, 4> policies{{
    {L"Диспетчер задач", L"Управление процессами и нагрузкой системы",
     L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"DisableTaskMgr", 1},
    {L"Редактор реестра", L"Доступ к редактору настроек Windows",
     L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"DisableRegistryTools", 2},
    {L"Командная строка", L"Доступ к командной строке Windows",
     L"Software\\Policies\\Microsoft\\Windows\\System", L"DisableCMD", 2},
    {L"Панель управления", L"Доступ к параметрам и панели управления",
     L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"NoControlPanel", 1}
}};

Health classify(const Policy& policy, const Observation& observation) {
    if (observation.state == ReadState::missing) return Health::clear;
    if (observation.state != ReadState::dword) return Health::unknown;
    if (observation.value == 0) return Health::clear;
    if (observation.value <= policy.max_block_value) return Health::blocked;
    return Health::unknown;
}

std::wstring registry_backup(const Policy& policy, std::uint32_t original) {
    std::wostringstream out;
    out << L"Windows Registry Editor Version 5.00\r\n\r\n"
        << L"[HKEY_CURRENT_USER\\" << policy.key << L"]\r\n"
        << L"\"" << policy.value << L"\"=dword:"
        << std::hex << std::setfill(L'0') << std::setw(8) << original << L"\r\n";
    return out.str();
}
}
