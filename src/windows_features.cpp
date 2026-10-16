#include "windows_features.h"
#include "font_recovery.h"
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <vector>

namespace nh {
namespace {
std::wstring settings_path() { return data_directory() + L"\\settings.ini"; }
std::wstring system_file(const wchar_t* name) {
    wchar_t path[MAX_PATH]{};
    if (!GetSystemDirectoryW(path, MAX_PATH)) return {};
    return std::wstring(path) + L"\\" + name;
}
bool make_directory(const std::wstring& path) {
    if (path.empty()) return false;
    const auto code = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return code == ERROR_SUCCESS || code == ERROR_ALREADY_EXISTS;
}
std::wstring unique_id() {
    GUID id{}; wchar_t buffer[40]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, buffer, 40)) return {};
    return buffer;
}
std::wstring file_hash(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32]{};
    bool good = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (good) good = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::vector<unsigned char> buffer(65536);
    DWORD received = 0;
    while (good) {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &received, nullptr)) { good = false; break; }
        if (!received) break;
        good = BCryptHashData(hash, buffer.data(), received, 0) >= 0;
    }
    if (good) good = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!good) return {};
    std::wostringstream out;
    for (auto byte : digest) out << std::hex << std::setfill(L'0') << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}
}

std::wstring data_directory() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) return {};
    std::wstring result = std::wstring(path) + L"\\CrimeSQL dev system recovery";
    CoTaskMemFree(path);
    return result;
}
std::wstring executable_path() {
    std::vector<wchar_t> path(32768);
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return size && size < path.size() ? std::wstring(path.data(), size) : std::wstring{};
}
bool write_unicode_file(const std::wstring& path, const std::wstring& contents, bool exclusive) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, exclusive ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const std::wstring bytes = L"\xFEFF" + contents;
    const DWORD size = static_cast<DWORD>(bytes.size() * sizeof(wchar_t));
    DWORD written = 0;
    bool good = WriteFile(file, bytes.data(), size, &written, nullptr) && written == size;
    if (good) good = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) good = false;
    return good;
}
Settings load_settings() {
    Settings result;
    const auto path = settings_path();
    result.animations = GetPrivateProfileIntW(L"UI", L"Animations", 1, path.c_str()) != 0;
    result.high_priority = GetPrivateProfileIntW(L"UI", L"HighPriority", 0, path.c_str()) != 0;
    auto candidate = result.bindings;
    bool valid = true;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        const auto key = L"Key" + std::to_wstring(i), modifiers = L"Modifiers" + std::to_wstring(i);
        candidate[i] = {GetPrivateProfileIntW(L"Hotkeys", modifiers.c_str(), candidate[i].modifiers, path.c_str()),
                        GetPrivateProfileIntW(L"Hotkeys", key.c_str(), candidate[i].key, path.c_str())};
        valid = valid && valid_binding(candidate[i]);
        for (std::size_t j = 0; j < i; ++j) valid = valid && !same_binding(candidate[i], candidate[j]);
    }
    if (valid) result.bindings = candidate;
    return result;
}
Result save_settings(const Settings& settings) {
    for (std::size_t i = 0; i < settings.bindings.size(); ++i) {
        if (!valid_binding(settings.bindings[i])) return {false, L"Недопустимое сочетание клавиш."};
        for (std::size_t j = 0; j < i; ++j)
            if (same_binding(settings.bindings[i], settings.bindings[j])) return {false, L"Назначьте разные сочетания."};
    }
    const auto directory = data_directory(), id = unique_id();
    if (directory.empty() || id.empty() || !make_directory(directory)) return {false, L"Не удалось создать папку настроек."};
    std::wstring contents = L"[UI]\r\nAnimations=" + std::to_wstring(settings.animations) + L"\r\nHighPriority=" + std::to_wstring(settings.high_priority) + L"\r\n[Hotkeys]\r\n";
    for (std::size_t i = 0; i < settings.bindings.size(); ++i)
        contents += L"Key" + std::to_wstring(i) + L"=" + std::to_wstring(settings.bindings[i].key) +
            L"\r\nModifiers" + std::to_wstring(i) + L"=" + std::to_wstring(settings.bindings[i].modifiers) + L"\r\n";
    const auto temporary = directory + L"\\" + id + L".tmp";
    if (!write_unicode_file(temporary, contents, true)) return {false, L"Не удалось записать настройки."};
    if (!MoveFileExW(temporary.c_str(), settings_path().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError(); DeleteFileW(temporary.c_str());
        return {false, L"Не удалось сохранить настройки: " + error_text(error)};
    }
    return {true, L"Настройки сохранены."};
}
bool autorun_enabled() {
    wchar_t value[32768]{};
    DWORD size = sizeof(value);
    const auto code = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"CrimeSQLRecovery", RRF_RT_REG_SZ, nullptr, value, &size);
    return code == ERROR_SUCCESS && autorun_command(executable_path()) == value;
}
Result set_autorun(bool enabled) {
    HKEY key = nullptr;
    auto code = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr,
        0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (code != ERROR_SUCCESS) return {false, error_text(code)};
    const auto value = autorun_command(executable_path());
    if (value.empty()) { RegCloseKey(key); return {false, L"Не удалось определить путь приложения."}; }
    code = enabled ? RegSetValueExW(key, L"CrimeSQLRecovery", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) : RegDeleteValueW(key, L"CrimeSQLRecovery");
    RegCloseKey(key);
    if (code != ERROR_SUCCESS && !(code == ERROR_FILE_NOT_FOUND && !enabled)) return {false, error_text(code)};
    return {true, enabled ? L"Автозапуск включён для текущего пользователя. Не перемещайте папку приложения."
        : L"Автозапуск CrimeSQL отключён."};
}
Result set_high_priority(bool enabled) {
    if (!SetPriorityClass(GetCurrentProcess(), enabled ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS))
        return {false, L"Не удалось изменить приоритет: " + error_text(GetLastError())};
    return {true, enabled ? L"Высокий приоритет установлен только для CrimeSQL. Это не защита от вирусов." : L"Обычный приоритет восстановлен."};
}
Result recover_mouse() {
    const bool released = ClipCursor(nullptr) != FALSE;
    const DWORD clip_error = released ? ERROR_SUCCESS : GetLastError();
    const bool own_block = BlockInput(FALSE) != FALSE;
    return {released, (released ? std::wstring(L"Ограничение области движения курсора сброшено.") : L"Сброс ограничения курсора не выполнен: " + error_text(clip_error)) +
        (own_block ? L"\nСобственная блокировка ввода снята." : L"\nЧужую блокировку BlockInput или перехватчик приложение снять не может.") +
        L"\nЕсли мышь всё ещё заблокирована, нажмите Ctrl+Alt+Del."};
}
Result recover_keyboard() {
    FILTERKEYS filter{}; filter.cbSize = sizeof(filter);
    if (!SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(filter), &filter, 0)) return {false, error_text(GetLastError())};
    const bool was_enabled = (filter.dwFlags & FKF_FILTERKEYSON) != 0;
    filter.dwFlags &= ~static_cast<DWORD>(FKF_FILTERKEYSON);
    if (was_enabled && !SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof(filter), &filter, SPIF_SENDCHANGE))
        return {false, L"Не удалось отключить фильтрацию клавиш: " + error_text(GetLastError())};
    const bool own_block = BlockInput(FALSE) != FALSE;
    return {true, std::wstring(was_enabled ? L"Фильтрация клавиш отключена на текущий сеанс." : L"Фильтрация клавиш уже выключена.") +
        (own_block ? L"\nСобственная блокировка ввода снята." : L"\nСнятие чужой блокировки ввода не подтверждено.") +
        L"\nПриложение не удаляет драйверы и чужие перехватчики клавиатуры. При полной блокировке нажмите Ctrl+Alt+Del."};
}
Result start_defender_scan(HWND owner, HANDLE& process) {
    const auto executable = system_file(L"WindowsPowerShell\\v1.0\\powershell.exe");
    SHELLEXECUTEINFOW run{}; run.cbSize = sizeof(run);
    run.hwnd = owner; run.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI; run.lpVerb = L"runas";
    run.lpFile = executable.c_str();
    run.lpParameters = L"-NoProfile -NonInteractive -Command \"& { try { Start-MpScan -ScanType FullScan -ErrorAction Stop; exit 0 } catch { Write-Error $_; exit 1 } }\"";
    run.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&run)) return {false, L"Проверка не запущена: " + error_text(GetLastError())};
    process = run.hProcess;
    return {true, L"Команда полной проверки Defender запущена. Результат ищите в «Безопасности Windows»."};
}
bool sandbox_available() {
    const auto path = system_file(L"WindowsSandbox.exe");
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
LabResult launch_sandbox(HWND owner, const std::wstring& sample) {
    if (!sandbox_available()) return {false, L"Windows Sandbox недоступна. Нужны Windows Pro/Enterprise/Education, виртуализация и включённый компонент «Песочница Windows». Файл не запускался.", {}};
    const auto exe = executable_path();
    const auto observer = exe.substr(0, exe.find_last_of(L"\\/")) + L"\\CrimeSQLObserver.exe";
    if (GetFileAttributesW(observer.c_str()) == INVALID_FILE_ATTRIBUTES)
        return {false, L"Рядом с приложением не найден CrimeSQLObserver.exe. Распакуйте весь ZIP.", {}};
    if (sample.size() < 4 || _wcsicmp(sample.c_str() + sample.size() - 4, L".exe") != 0)
        return {false, L"Выберите исполняемый файл .exe.", {}};
    const auto directory = data_directory(), id = unique_id();
    if (directory.empty() || id.empty()) return {false, L"Не удалось подготовить сеанс.", {}};
    const auto session = directory + L"\\LabSessions\\" + id;
    const auto input = session + L"\\Input";
    if (!make_directory(input)) return {false, L"Не удалось создать папку сеанса.", {}};
    const auto copied = input + L"\\sample.exe";
    if (!CopyFileW(sample.c_str(), copied.c_str(), TRUE) || !CopyFileW(observer.c_str(), (input + L"\\CrimeSQLObserver.exe").c_str(), TRUE))
        return {false, L"Не удалось скопировать файлы. Исходный EXE не запускался: " + error_text(GetLastError()), {}};
    const auto hash = file_hash(copied);
    if (hash.empty()) return {false, L"Не удалось вычислить SHA-256. Файл не запускался.", {}};
    const auto configuration = session + L"\\CrimeSQL-Lab.wsb";
    if (!write_unicode_file(configuration, sandbox_config(input), true)) return {false, L"Не удалось сохранить конфигурацию Sandbox.", {}};
    const auto executable = system_file(L"WindowsSandbox.exe");
    const auto arguments = L"\"" + configuration + L"\"";
    SHELLEXECUTEINFOW launch{}; launch.cbSize = sizeof(launch); launch.hwnd = owner;
    launch.fMask = SEE_MASK_FLAG_NO_UI; launch.lpFile = executable.c_str(); launch.lpParameters = arguments.c_str(); launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch)) return {false, L"Не удалось открыть Sandbox. Исходный файл на основном ПК не запускался: " + error_text(GetLastError()), hash};
    return {true, L"Конфигурация передана Windows Sandbox. Образец запускается внутри неё только после готовности наблюдателя. Журнал находится в окне CrimeSQLObserver внутри Sandbox.\n\nSHA-256: " + hash +
        L"\n\nКопия образца остаётся в " + session + L". После закрытия Sandbox удалите папку сеанса, если она больше не нужна.", hash};
}
std::wstring binding_name(Binding b) {
    std::wstring out;
    if (b.modifiers & MOD_CONTROL) out += L"Ctrl + ";
    if (b.modifiers & MOD_ALT) out += L"Alt + ";
    if (b.modifiers & MOD_SHIFT) out += L"Shift + ";
    if (b.key >= VK_F1 && b.key <= VK_F24) out += L"F" + std::to_wstring(b.key - VK_F1 + 1);
    else out += static_cast<wchar_t>(b.key);
    return out;
}
std::wstring startup_audit() {
    std::wstring report=L"CrimeSQL dev system recovery — аудит автозагрузки\r\n"
        L"Только Run и RunOnce в HKCU/HKLM, включая 32-битное представление HKLM.\r\n"
        L"Это не полный анализ служб, задач планировщика и других способов запуска.\r\n"
        L"Записи не удаляются. Команда в автозагрузке сама по себе не доказывает заражение.\r\n"
        L"Отчёт может содержать личные пути и аргументы команд: проверьте его перед отправкой.\r\n\r\n";
    for(int root_index=0;root_index<3;++root_index) for(const wchar_t* suffix:{L"Run",L"RunOnce"}) {
        const auto path=L"Software\\Microsoft\\Windows\\CurrentVersion\\"+std::wstring(suffix);
        const auto label=std::wstring(root_index==0?L"HKCU":root_index==1?L"HKLM (64/native)":L"HKLM (32-bit)")+L"\\"+path;
        report+=L"["+label+L"]\r\n";
        HKEY key=nullptr;
        const auto code=RegOpenKeyExW(root_index==0?HKEY_CURRENT_USER:HKEY_LOCAL_MACHINE,path.c_str(),0,
            KEY_QUERY_VALUE|(root_index==2?KEY_WOW64_32KEY:0),&key);
        if(code!=ERROR_SUCCESS) { report+=code==ERROR_FILE_NOT_FOUND?L"Ключ отсутствует.\r\n\r\n":L"Недоступно: "+error_text(code)+L"\r\n\r\n"; continue; }
        bool any=false;
        std::vector<wchar_t> name(16384),data(32768);
        for(DWORD index=0;index<256;++index) {
            DWORD name_size=static_cast<DWORD>(name.size()),data_size=static_cast<DWORD>(data.size()*sizeof(wchar_t)),type=0;
            const auto status=RegEnumValueW(key,index,name.data(),&name_size,nullptr,&type,reinterpret_cast<BYTE*>(data.data()),&data_size);
            if(status==ERROR_NO_MORE_ITEMS) break;
            if(status!=ERROR_SUCCESS) { report+=L"Запись недоступна: "+error_text(status)+L"\r\n"; continue; }
            any=true;
            report+=std::wstring(name.data(),name_size)+L" = ";
            if((type==REG_SZ||type==REG_EXPAND_SZ)&&data_size%sizeof(wchar_t)==0) {
                const auto count=data_size/sizeof(wchar_t);
                const auto end=std::find(data.begin(),data.begin()+count,L'\0');
                report+=std::wstring(data.begin(),end);
            } else report+=L"<нетекстовое значение>";
            report+=L"\r\n";
            if(index==255) report+=L"Достигнут лимит 256 записей для ключа.\r\n";
        }
        RegCloseKey(key);
        if(!any) report+=L"Нет записей.\r\n";
        report+=L"\r\n";
    }
    return report;
}
Result launch_recovery_action(HWND owner,RecoveryAction action,HANDLE& process) {
    std::wstring executable,arguments,verb=L"open";
    auto system_directory=system_file(L"");
    if(system_directory.empty()) return {false,L"Не удалось определить системную папку Windows."};
    system_directory.pop_back();
    const auto script=recovery_script(action,system_directory);
    if(action==RecoveryAction::defender_ui) executable=L"windowsdefender:";
    else if(action==RecoveryAction::advanced_startup) executable=L"ms-settings:recovery";
    else if(action==RecoveryAction::startup_settings) executable=L"ms-settings:startupapps";
    else if(action==RecoveryAction::font_settings) executable=L"ms-settings:fonts";
    else if(action==RecoveryAction::restore_fonts) {
        const auto sid=current_user_sid();
        if(sid.empty()) return {false,L"Не удалось определить пользователя; шрифты не изменены."};
        executable=executable_path(); arguments=L"--restore-fonts "+sid; verb=L"runas";
    }
    else if(action==RecoveryAction::system_info) executable=system_file(L"msinfo32.exe");
    else if(action==RecoveryAction::restore_points) executable=system_file(L"rstrui.exe");
    else if(action==RecoveryAction::powershell_admin) {
        executable=system_file(L"WindowsPowerShell\\v1.0\\powershell.exe"); arguments=L"-NoLogo -NoProfile -NoExit"; verb=L"runas";
    } else if(action==RecoveryAction::cmd_admin) {
        executable=system_file(L"cmd.exe"); arguments=L"/d"; verb=L"runas";
    } else if(action==RecoveryAction::startup_audit||action==RecoveryAction::diagnostic_report) {
        const auto id=unique_id(),directory=data_directory()+L"\\Reports";
        if(id.empty()||!make_directory(directory)) return {false,L"Не удалось создать папку отчётов."};
        std::wstring contents;
        if(action==RecoveryAction::startup_audit) contents=startup_audit();
        else {
            contents=L"CrimeSQL — диагностика\r\n"+system_summary()+L"\r\nWinDefend: "+defender_status()+L"\r\n";
            for(const auto& policy:policies) {
                const auto observation=read_policy(policy); const auto health=classify(policy,observation);
                contents+=std::wstring(policy.name)+L": "+(health==Health::clear?L"ограничений не найдено":health==Health::blocked?L"ограничение":L"неизвестно/ошибка")+L"\r\n";
            }
            contents+=L"\r\nЭто проверка настроек, а не результат антивирусного сканирования.\r\n";
        }
        const auto file=directory+L"\\"+id+L".txt";
        if(!write_unicode_file(file,contents,true)) return {false,L"Не удалось сохранить отчёт."};
        executable=system_file(L"notepad.exe"); arguments=L"\""+file+L"\"";
    } else if(!script.empty()) {
        const auto directory=data_directory()+L"\\Reports",id=unique_id();
        if(id.empty()||!make_directory(directory)) return {false,L"Не удалось подготовить журнал операции."};
        const auto transcript=directory+L"\\"+id+L".txt";
        const auto body=maintenance_wrapper(script,transcript);
        executable=system_file(L"WindowsPowerShell\\v1.0\\powershell.exe");
        arguments=L"-NoLogo -NoProfile -Command \""+body+L"\""; verb=L"runas";
    } else return {false,L"Неизвестная операция."};
    SHELLEXECUTEINFOW launch{}; launch.cbSize=sizeof(launch); launch.hwnd=owner;
    const bool tracked=!script.empty()||action==RecoveryAction::restore_fonts;
    launch.fMask=SEE_MASK_FLAG_NO_UI|(tracked?SEE_MASK_NOCLOSEPROCESS:0);
    launch.lpVerb=verb.c_str(); launch.lpFile=executable.c_str(); launch.lpParameters=arguments.empty()?nullptr:arguments.c_str(); launch.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&launch)) return {false,L"Операция не запущена: "+error_text(GetLastError())};
    if(tracked) process=launch.hProcess;
    if(action==RecoveryAction::restore_fonts) return {true,L"Открыто отдельное окно восстановления шрифтов. Результат и путь к копии Backups будут показаны в нём. Для применения потребуется повторный вход в Windows."};
    return {true,script.empty()?L"Запрос открытия инструмента передан Windows.":
        L"Окно операции открыто. Результат и код завершения показаны в нём; журнал сохраняется в Reports. Это не подтверждение отсутствия угроз."};
}
void apply_window_theme(HWND window) {
    BOOL dark=TRUE;
    if(FAILED(DwmSetWindowAttribute(window,20,&dark,sizeof(dark))))
        DwmSetWindowAttribute(window,19,&dark,sizeof(dark));
    const DWORD rounded=2;
    DwmSetWindowAttribute(window,33,&rounded,sizeof(rounded));
}
}
