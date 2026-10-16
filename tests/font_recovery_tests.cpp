#include "font_recovery.h"
#include <sddl.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
struct State { DWORD type; std::vector<BYTE> bytes; bool exists; };
State read(const std::wstring& path, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
    State value{}; DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &value.type, nullptr, &size) == ERROR_SUCCESS) {
        value.exists = true; value.bytes.resize(size); BYTE dummy = 0;
        check(RegQueryValueExW(key, name, nullptr, &value.type, size ? value.bytes.data() : &dummy, &size) == ERROR_SUCCESS, "Read test value");
        value.bytes.resize(size);
    }
    RegCloseKey(key); return value;
}
void set(const std::wstring& path, const wchar_t* name, DWORD type, const void* bytes, DWORD size) {
    HKEY key = nullptr;
    check(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr) == ERROR_SUCCESS, "Create disposable font key");
    const auto code = RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(bytes), size);
    RegCloseKey(key); check(code == ERROR_SUCCESS, "Set disposable font value");
}
void text(const std::wstring& path, const wchar_t* name, const wchar_t* value) {
    set(path, name, REG_SZ, value, static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
}
struct Fixture {
    std::wstring key = L"Software\\CrimeSQL dev system recovery\\FontTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    std::wstring directory;
    nh::FontRecoveryTarget target;
    Fixture() {
        wchar_t temp[MAX_PATH]{}; check(GetTempPathW(MAX_PATH, temp) != 0, "Temporary folder available");
        directory = std::wstring(temp) + L"CrimeSQL-Fonts-" + std::to_wstring(GetCurrentProcessId());
        check(CreateDirectoryW(directory.c_str(), nullptr) != FALSE, "Create disposable font folder");
        target = {HKEY_CURRENT_USER, key + L"\\Machine", HKEY_CURRENT_USER, key + L"\\User", key + L"\\Metrics", directory, directory + L"\\before.reg", 144};
        for (const auto file : {L"segoeui.ttf", L"segoeuib.ttf", L"segoeuii.ttf", L"segoeuiz.ttf", L"SegUIVar.ttf"})
            check(nh::write_unicode_file(directory + L"\\" + file, L"Harmless fixture; NOT a real font", true), "Create harmless presence-check fixture");
    }
    ~Fixture() {
        RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
        WIN32_FIND_DATAW found{};
        HANDLE listing = FindFirstFileW((directory + L"\\*").c_str(), &found);
        if (listing != INVALID_HANDLE_VALUE) {
            do { if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((directory + L"\\" + found.cFileName).c_str()); } while (FindNextFileW(listing, &found));
            FindClose(listing);
        }
        RemoveDirectoryW(directory.c_str());
    }
};
bool import_backup(const std::wstring& path) {
    wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
    const auto executable = std::wstring(system) + L"\\reg.exe";
    auto command = L"\"" + executable + L"\" import \"" + path + L"\" /reg:64";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
    const auto waited = WaitForSingleObject(process.hProcess, 10000); DWORD code = 1;
    if (waited == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
    else GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return waited == WAIT_OBJECT_0 && code == 0;
}
}
int main() {
    try {
        Fixture f;
        const auto machine_fonts = f.target.machine_base + L"\\Fonts", machine_sub = f.target.machine_base + L"\\FontSubstitutes";
        const auto user_fonts = f.target.user_base + L"\\Fonts", user_sub = f.target.user_base + L"\\FontSubstitutes";
        text(machine_fonts, L"Segoe UI (TrueType)", L"");
        text(machine_sub, L"Segoe UI", L"Wingdings"); text(machine_sub, L"Arial", L"Unrelated font");
        text(machine_sub, L"Segoe UI Variable Display", L"Symbol");
        text(user_fonts, L"Segoe UI (TrueType)", L"broken.ttf");
        const BYTE malformed[]{0x61, 0x00, 0xff};
        set(user_sub, L"Segoe UI", REG_EXPAND_SZ, malformed, sizeof(malformed));
        check(!nh::restore_fonts_at(f.target).success && read(machine_sub, L"Segoe UI").exists &&
            GetFileAttributesW(f.target.backup_file.c_str()) == INVALID_FILE_ATTRIBUTES, "Reject malformed string without promising lossless reg.exe rollback");
        set(user_sub, L"Segoe UI", REG_BINARY, malformed, sizeof(malformed));
        set(f.target.metrics_key, L"MenuFont", REG_BINARY, malformed, sizeof(malformed));
        set(f.target.metrics_key, L"CaptionFont", REG_BINARY, nullptr, 0);
        const DWORD original = 123;
        set(f.target.metrics_key, L"MessageFont", REG_DWORD, &original, sizeof(original));
        text(user_fonts, L"My Custom Font (TrueType)", L"keep.ttf");
        auto blocked = f.target; blocked.backup_file = f.directory + L"\\missing\\before.reg";
        check(!nh::restore_fonts_at(blocked).success, "Backup failure aborts before edits");
        check(read(machine_sub, L"Segoe UI").exists && read(user_fonts, L"Segoe UI (TrueType)").exists, "Backup failure preserves replacements");
        auto invalid_dpi = f.target; invalid_dpi.dpi = 0;
        check(!nh::restore_fonts_at(invalid_dpi).success, "Invalid font DPI rejected");
        check(MoveFileW((f.directory + L"\\segoeui.ttf").c_str(), (f.directory + L"\\saved.ttf").c_str()) != FALSE, "Hide fixture required file");
        check(!nh::restore_fonts_at(f.target).success && GetFileAttributesW(f.target.backup_file.c_str()) == INVALID_FILE_ATTRIBUTES, "Missing core file aborts before backup and changes");
        check(MoveFileW((f.directory + L"\\saved.ttf").c_str(), (f.directory + L"\\segoeui.ttf").c_str()) != FALSE, "Restore fixture file");
        const auto repaired = nh::restore_fonts_at(f.target);
        if (!repaired.success) std::wcerr << repaired.message << '\n';
        check(repaired.success, "Real disposable registry repair succeeds");
        check(!read(machine_sub, L"Segoe UI").exists && !read(user_sub, L"Segoe UI").exists, "Remove both machine and user font substitutions");
        check(!read(machine_sub, L"Segoe UI Variable Display").exists, "Remove installed Windows 11 variable-font substitution");
        check(!read(user_fonts, L"Segoe UI (TrueType)").exists, "Remove per-user override of system font");
        const auto registration = read(machine_fonts, L"Segoe UI (TrueType)");
        check(registration.exists && registration.type == REG_SZ && registration.bytes.size() == sizeof(L"segoeui.ttf") &&
            std::memcmp(registration.bytes.data(), L"segoeui.ttf", sizeof(L"segoeui.ttf")) == 0, "Restore system Segoe UI registration");
        check(!read(machine_fonts, L"Segoe UI Light (TrueType)").exists, "Do not register absent optional fonts");
        for (const auto name : {L"CaptionFont", L"SmCaptionFont", L"MenuFont", L"StatusFont", L"MessageFont", L"IconFont"}) {
            const auto state = read(f.target.metrics_key, name); LOGFONTW font{};
            check(state.type == REG_BINARY && state.bytes.size() == sizeof(font), "WindowMetrics is exact LOGFONTW binary");
            std::memcpy(&font, state.bytes.data(), sizeof(font));
            check(std::wstring(font.lfFaceName) == L"Segoe UI" && font.lfHeight == -18 && font.lfWeight == FW_NORMAL, "Readable 9pt interface font at 144 DPI");
        }
        check(read(machine_sub, L"Arial").exists && read(user_fonts, L"My Custom Font (TrueType)").exists, "Unrelated font settings preserved");
        check(!nh::restore_fonts_at(f.target).success, "Existing backup is never overwritten");
        check(import_backup(f.target.backup_file), "Real reg.exe imports generated UTF-16 backup");
        const auto restored = read(user_sub, L"Segoe UI"), metric = read(f.target.metrics_key, L"MenuFont");
        check(restored.type == REG_BINARY && restored.bytes == std::vector<BYTE>(malformed, malformed + sizeof(malformed)), "Backup round trip preserves arbitrary original binary bytes");
        check(metric.type == REG_BINARY && metric.bytes == std::vector<BYTE>(malformed, malformed + sizeof(malformed)), "Backup restores original binary metrics");
        const auto substitution = read(machine_sub, L"Segoe UI");
        check(substitution.type == REG_SZ && substitution.bytes.size() == sizeof(L"Wingdings") &&
            std::memcmp(substitution.bytes.data(), L"Wingdings", sizeof(L"Wingdings")) == 0, "Backup restores original Unicode font replacement exactly");
        check(read(f.target.metrics_key, L"CaptionFont").exists && read(f.target.metrics_key, L"CaptionFont").bytes.empty(), "Backup preserves zero-length value");
        const auto message = read(f.target.metrics_key, L"MessageFont");
        check(message.type == REG_DWORD && message.bytes.size() == sizeof(original) && std::memcmp(message.bytes.data(), &original, sizeof(original)) == 0, "Backup restores original DWORD type and bytes");
        check(!read(f.target.metrics_key, L"IconFont").exists && !read(machine_fonts, L"Segoe UI Bold (TrueType)").exists, "Backup deletes values absent before repair");
        // Deny only SET_VALUE on the final metrics key: earlier edits must roll back on failure.
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        check(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x2;;;WD)(A;;KA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr) != FALSE, "Build disposable write-denying DACL");
        HKEY metrics = nullptr;
        check(RegOpenKeyExW(HKEY_CURRENT_USER, f.target.metrics_key.c_str(), 0, WRITE_DAC | KEY_READ, &metrics) == ERROR_SUCCESS, "Open disposable metrics security");
        const auto secured = RegSetKeySecurity(metrics, DACL_SECURITY_INFORMATION, descriptor);
        LocalFree(descriptor);
        HKEY probe = nullptr;
        const auto denied = RegOpenKeyExW(HKEY_CURRENT_USER, f.target.metrics_key.c_str(), 0, KEY_SET_VALUE, &probe);
        if (probe) RegCloseKey(probe);
        const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
        bool failed = false;
        if (secured == ERROR_SUCCESS && denied == ERROR_ACCESS_DENIED) {
            auto target = f.target; target.backup_file = f.directory + L"\\rollback.reg";
            failed = !nh::restore_fonts_at(target).success;
        }
        SECURITY_DESCRIPTOR open{}; InitializeSecurityDescriptor(&open, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&open, TRUE, nullptr, FALSE);
        const auto unlocked = RegSetKeySecurity(metrics, DACL_SECURITY_INFORMATION, &open);
        RegCloseKey(metrics);
        check(unlocked == ERROR_SUCCESS, "Remove disposable DACL before cleanup");
        if (wine && denied != ERROR_ACCESS_DENIED) std::cout << "Wine registry ACL enforcement unavailable; write-denial rollback check SKIPPED\n";
        else {
            check(secured == ERROR_SUCCESS && denied == ERROR_ACCESS_DENIED && failed, "Write denial makes transaction fail");
            const auto after = read(machine_fonts, L"Segoe UI (TrueType)");
            check(after.bytes == std::vector<BYTE>(sizeof(wchar_t), 0) && read(machine_sub, L"Segoe UI").exists && read(user_fonts, L"Segoe UI (TrueType)").exists, "Earlier writes roll back after late permission failure");
        }
        check(!nh::current_user_sid().empty(), "Resolve current user's actual SID");
        check(!nh::restore_system_fonts(L"invalid-sid").success, "Malformed helper SID cannot touch real font settings");
        std::cout << checks << " font recovery checks passed on disposable registry keys and files\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Font check " << checks << " failed: " << error.what() << '\n'; return 1; }
}
