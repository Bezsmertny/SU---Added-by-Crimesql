#include "recovery.h"
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool passed, const char* message) {
    ++checks;
    if (!passed) throw std::runtime_error(message);
}
}

int main() {
    using namespace nh;
    try {
        for (const auto& policy : policies) {
            check(classify(policy, {ReadState::missing, 0, 0}) == Health::clear, "Missing policy must be clear");
            check(classify(policy, {ReadState::dword, 0, 0}) == Health::clear, "Zero must be clear");
            for (std::uint32_t value = 1; value <= policy.max_block_value; ++value)
                check(classify(policy, {ReadState::dword, value, 0}) == Health::blocked, "Documented restriction must be repairable");
            check(classify(policy, {ReadState::dword, policy.max_block_value + 1, 0}) == Health::unknown, "Unknown DWORD must not be repairable");
            check(classify(policy, {ReadState::dword, 0xffffffff, 0}) == Health::unknown, "Unexpected DWORD must remain unknown");
            for (auto state : {ReadState::unsupported_type, ReadState::denied, ReadState::error})
                check(classify(policy, {state, 1, 5}) == Health::unknown, "Unreadable policy must not be clear or repairable");
            auto backup = registry_backup(policy, policy.max_block_value);
            check(backup.find(L"[HKEY_CURRENT_USER\\" + std::wstring(policy.key) + L"]\r\n") != std::wstring::npos,
                  "Backup must target original HKCU key");
            check(backup.find(L"\"" + std::wstring(policy.value) + L"\"=dword:") != std::wstring::npos,
                  "Backup must preserve original value name");
        }
        const auto expected = L"Windows Registry Editor Version 5.00\r\n\r\n"
            L"[HKEY_CURRENT_USER\\Software\\Policies\\Microsoft\\Windows\\System]\r\n"
            L"\"DisableCMD\"=dword:00000002\r\n";
        check(registry_backup(policies[2], 2) == expected, "REG export must preserve original DWORD and CRLF");
        check(registry_backup(policies[0], 0x1234abcd).find(L"dword:1234abcd\r\n") != std::wstring::npos,
              "Backup DWORD must be hexadecimal without truncation");
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Check " << checks << " failed: " << error.what() << '\n';
        return 1;
    }
}
