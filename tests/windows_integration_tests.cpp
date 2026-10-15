#include "windows_system.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    HKEY handle = nullptr;
    std::wstring path = L"Software\\NEDOHACKERS lite\\IntegrationTest-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64());
    std::vector<std::wstring> files;
    Fixture() {
        DWORD disposition = 0;
        auto code = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &handle, &disposition);
        if (code != ERROR_SUCCESS || disposition != REG_CREATED_NEW_KEY) {
            if (handle) RegCloseKey(handle);
            handle = nullptr;
            throw std::runtime_error("Cannot create isolated registry test key");
        }
    }
    ~Fixture() {
        for (const auto& file : files) DeleteFileW(file.c_str());
        if (handle) { RegCloseKey(handle); RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str()); }
    }
    void set(DWORD value) {
        check(RegSetValueExW(handle, L"TestRestriction", 0, REG_DWORD, reinterpret_cast<BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS,
              "Set temporary test DWORD");
    }
};
}

int main() {
    try {
        Fixture fixture;
        nh::Policy policy{L"Test", L"Disposable test policy", fixture.path.c_str(), L"TestRestriction", 2};
        check(nh::read_policy(policy).state == nh::ReadState::missing, "Missing temporary value");
        check(!nh::restore_policy(policy, {nh::ReadState::missing, 0, 0}).success, "No repair for missing policy");
        for (DWORD value : {1ul, 2ul}) {
            fixture.set(value);
            const auto before = nh::read_policy(policy);
            check(before.state == nh::ReadState::dword && before.value == value, "Read actual temporary DWORD");
            const auto result = nh::restore_policy(policy, before);
            if (!result.backup_path.empty()) fixture.files.push_back(result.backup_path);
            check(result.success, "Restore temporary policy");
            check(nh::read_policy(policy).state == nh::ReadState::missing, "Restoration removes only selected value");
            HANDLE file = CreateFileW(result.backup_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            check(file != INVALID_HANDLE_VALUE, "Backup must exist");
            const auto expected = L"\xFEFF" + nh::registry_backup(policy, value);
            std::vector<wchar_t> contents(expected.size());
            DWORD received = 0;
            const bool read = ReadFile(file, contents.data(), static_cast<DWORD>(contents.size() * sizeof(wchar_t)), &received, nullptr) != FALSE;
            const DWORD size = GetFileSize(file, nullptr);
            CloseHandle(file);
            check(read && received == expected.size() * sizeof(wchar_t) && size == received, "Complete UTF-16 backup");
            check(std::wstring(contents.begin(), contents.end()) == expected, "Exact backup including BOM and original value");
        }
        fixture.set(1);
        auto stale = nh::read_policy(policy);
        fixture.set(2);
        check(!nh::restore_policy(policy, stale).success, "Reject stale observations");
        check(nh::read_policy(policy).value == 2, "Stale repair must preserve newer value");
        fixture.set(99);
        check(!nh::restore_policy(policy, nh::read_policy(policy)).success, "Do not repair unknown values");
        const wchar_t wrong_type[] = L"1";
        check(RegSetValueExW(fixture.handle, policy.value, 0, REG_SZ, reinterpret_cast<const BYTE*>(wrong_type), sizeof(wrong_type)) == ERROR_SUCCESS,
              "Set temporary string value");
        check(nh::read_policy(policy).state == nh::ReadState::unsupported_type, "Do not interpret strings as DWORDs");
        check(!nh::restore_policy(policy, nh::read_policy(policy)).success, "Do not repair unsupported types");
        std::cout << checks << " Windows integration checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Check " << checks << " failed: " << error.what() << '\n';
        return 1;
    }
}
