#include "windows_system.h"
#include <shlobj.h>
#include <shellapi.h>
#include <cwchar>

namespace nh {
namespace {
struct Key { HKEY value = nullptr; ~Key() { if (value) RegCloseKey(value); } };
struct Service { SC_HANDLE value; ~Service() { if (value) CloseServiceHandle(value); } };
Observation read_key(HKEY key, const Policy& policy) {
    DWORD type = 0, value = 0, size = sizeof(value);
    LSTATUS code = RegQueryValueExW(key, policy.value, nullptr, &type,
                                   reinterpret_cast<BYTE*>(&value), &size);
    if (code == ERROR_FILE_NOT_FOUND) return {ReadState::missing, 0, 0};
    if (code == ERROR_MORE_DATA) return {ReadState::unsupported_type, 0, static_cast<DWORD>(code)};
    if (code != ERROR_SUCCESS) return {code == ERROR_ACCESS_DENIED ? ReadState::denied : ReadState::error, 0, static_cast<DWORD>(code)};
    if (type != REG_DWORD || size != sizeof(value)) return {ReadState::unsupported_type, 0, 0};
    return {ReadState::dword, value, 0};
}
}

std::wstring error_text(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = buffer ? buffer : L"Неизвестная ошибка";
    if (buffer) LocalFree(buffer);
    return result + L" (" + std::to_wstring(code) + L")";
}

Observation read_policy(const Policy& policy) {
    Key key;
    const LSTATUS code = RegOpenKeyExW(HKEY_CURRENT_USER, policy.key, 0, KEY_QUERY_VALUE, &key.value);
    if (code == ERROR_FILE_NOT_FOUND) return {ReadState::missing, 0, 0};
    if (code != ERROR_SUCCESS) return {code == ERROR_ACCESS_DENIED ? ReadState::denied : ReadState::error, 0, static_cast<DWORD>(code)};
    return read_key(key.value, policy);
}

std::wstring backup_directory() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) return {};
    std::wstring result = std::wstring(path) + L"\\CrimeSQL dev system recovery\\Backups";
    CoTaskMemFree(path);
    return result;
}

RepairResult restore_policy(const Policy& policy, const Observation& expected) {
    if (classify(policy, expected) != Health::blocked)
        return {false, L"Восстановление недоступно: нет распознанного ограничения."};
    Key key;
    LSTATUS code = RegOpenKeyExW(HKEY_CURRENT_USER, policy.key, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key.value);
    if (code != ERROR_SUCCESS) return {false, L"Не удалось открыть настройку: " + error_text(code)};
    const auto current = read_key(key.value, policy);
    if (current.state != expected.state || current.value != expected.value)
        return {false, L"Настройка изменилась после проверки. Повторите диагностику."};

    const auto directory = backup_directory();
    if (directory.empty()) return {false, L"Не удалось найти папку резервных копий. Изменения отменены."};
    const int directory_result = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    if (directory_result != ERROR_SUCCESS && directory_result != ERROR_ALREADY_EXISTS)
        return {false, L"Не удалось создать папку резервных копий: " + error_text(directory_result)};
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t suffix[96]{};
    swprintf_s(suffix, L"\\%04u%02u%02u-%02u%02u%02u-%03u-%lu-", time.wYear, time.wMonth,
               time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId());
    GUID identifier{};
    wchar_t identifier_text[40]{};
    if (FAILED(CoCreateGuid(&identifier)) || !StringFromGUID2(identifier, identifier_text, 40))
        return {false, L"Не удалось создать уникальное имя резервной копии. Настройка не изменена."};
    const std::wstring filename = directory + suffix + policy.value + L"-" + identifier_text + L".reg";
    HANDLE file = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {false, L"Не удалось создать резервную копию: " + error_text(GetLastError())};
    const std::wstring contents = L"\xFEFF" + registry_backup(policy, current.value);
    const DWORD size = static_cast<DWORD>(contents.size() * sizeof(wchar_t));
    DWORD written = 0;
    const BOOL write_ok = WriteFile(file, contents.data(), size, &written, nullptr);
    DWORD file_error = write_ok ? (written == size ? ERROR_SUCCESS : ERROR_WRITE_FAULT) : GetLastError();
    bool saved = write_ok && written == size;
    if (saved && !FlushFileBuffers(file)) { saved = false; file_error = GetLastError(); }
    if (!CloseHandle(file)) { saved = false; file_error = GetLastError(); }
    if (!saved) return {false, L"Резервная копия не сохранена; настройка не изменена. " + error_text(file_error)};

    // Recheck after disk I/O: do not overwrite a change observed since confirmation.
    const auto latest = read_key(key.value, policy);
    if (latest.state != current.state || latest.value != current.value)
        return {false, L"Настройка изменилась во время сохранения. Изменения отменены. Копия: " + filename};
    code = RegDeleteValueW(key.value, policy.value);
    if (code != ERROR_SUCCESS) return {false, L"Копия: " + filename + L". Изменение не выполнено: " + error_text(code)};
    const auto after = read_key(key.value, policy);
    if (classify(policy, after) != Health::clear)
        return {false, L"Ограничение осталось или было восстановлено политикой. Копия: " + filename};
    return {true, L"Ограничение снято для текущего пользователя. Резервная копия: " + filename, filename};
}

std::wstring system_summary() {
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) return L"Сведения о памяти недоступны";
    const wchar_t* arch = info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? L"x64" :
        info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? L"ARM64" : L"x86";
    return std::wstring(arch) + L"  /  " + std::to_wstring(memory.ullTotalPhys / (1024ull * 1024 * 1024)) + L" ГБ RAM";
}

std::wstring defender_status() {
    Service manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!manager.value) return L"Статус службы недоступен";
    Service service{OpenServiceW(manager.value, L"WinDefend", SERVICE_QUERY_STATUS)};
    if (!service.value) return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? L"Служба не установлена" : L"Статус службы недоступен";
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(service.value, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status), &bytes))
        return L"Статус службы недоступен";
    return status.dwCurrentState == SERVICE_RUNNING ? L"Служба запущена" : L"Служба не запущена";
}

bool open_tool(HWND owner, Tool tool, std::wstring& error) {
    std::wstring target;
    if (tool == Tool::defender) target = L"windowsdefender:";
    else if (tool == Tool::recovery) target = L"ms-settings:recovery";
    else if (tool == Tool::backups) {
        target = backup_directory();
        if (target.empty() || GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
            error = L"Резервных копий пока нет. Они создаются перед восстановлением.";
            return false;
        }
    } else {
        wchar_t system[MAX_PATH]{};
        if (!GetSystemDirectoryW(system, MAX_PATH)) { error = error_text(GetLastError()); return false; }
        target = std::wstring(system) + (tool == Tool::task_manager ? L"\\Taskmgr.exe" : L"\\msinfo32.exe");
    }
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.hwnd = owner;
    execute.fMask = SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = L"open";
    execute.lpFile = target.c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&execute)) { error = error_text(GetLastError()); return false; }
    return true;
}
}
