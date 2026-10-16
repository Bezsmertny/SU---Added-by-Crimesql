#include "font_recovery.h"
#include <sddl.h>
#include <shlobj.h>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace nh {
namespace {
struct Key {
    HKEY value = nullptr;
    ~Key() { if (value) RegCloseKey(value); }
};
struct Value {
    bool exists = false;
    DWORD type = REG_NONE;
    std::vector<BYTE> bytes;
    bool operator==(const Value& other) const {
        return exists == other.exists && (!exists || (type == other.type && bytes == other.bytes));
    }
};
struct Edit {
    HKEY root;
    std::wstring key, name;
    Value before, after;
};
void require(LSTATUS code) {
    if (code != ERROR_SUCCESS) throw std::runtime_error(std::to_string(code));
}
bool missing(LSTATUS code) { return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND; }
Value read(HKEY root, const std::wstring& path, const std::wstring& name) {
    Key key;
    auto code = RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
    if (missing(code)) return {};
    require(code);
    Value value;
    DWORD length = 0;
    code = RegQueryValueExW(key.value, name.c_str(), nullptr, &value.type, nullptr, &length);
    if (missing(code)) return {};
    require(code);
    if (length > 65536) throw std::runtime_error("Oversized registry value");
    value.exists = true;
    value.bytes.resize(length);
    // A dummy byte prevents a zero-length buffer from becoming another size-only query.
    BYTE empty = 0;
    require(RegQueryValueExW(key.value, name.c_str(), nullptr, &value.type,
        length ? value.bytes.data() : &empty, &length));
    value.bytes.resize(length);
    return value;
}
void write(const Edit& edit, const Value& value) {
    Key key;
    auto code = RegOpenKeyExW(edit.root, edit.key.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key.value);
    if (missing(code)) {
        if (!value.exists) return;
        require(RegCreateKeyExW(edit.root, edit.key.c_str(), 0, nullptr, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key.value, nullptr));
    } else require(code);
    if (value.exists) require(RegSetValueExW(key.value, edit.name.c_str(), 0, value.type,
        value.bytes.data(), static_cast<DWORD>(value.bytes.size())));
    else {
        code = RegDeleteValueW(key.value, edit.name.c_str());
        if (!missing(code)) require(code);
    }
}
Value raw(DWORD type, const void* data, std::size_t size) {
    Value value; value.exists = true; value.type = type;
    const auto begin = static_cast<const BYTE*>(data);
    value.bytes.assign(begin, begin + size);
    return value;
}
Value string_value(const wchar_t* text) { return raw(REG_SZ, text, (wcslen(text) + 1) * sizeof(wchar_t)); }
std::wstring escaped(const std::wstring& text) {
    std::wstring result;
    for (const auto c : text) { if (c == L'\\' || c == L'"') result += L'\\'; result += c; }
    return result;
}
std::wstring registry_backup(const std::vector<Edit>& edits) {
    std::wostringstream out;
    out << L"Windows Registry Editor Version 5.00\r\n\r\n"
        L"; CrimeSQL: original font values. Review before importing.\r\n"
        L"; HKEY_USERS entries target the original user's SID, even after alternate-account UAC.\r\n\r\n";
    for (const auto& edit : edits) {
        const auto root = edit.root == HKEY_LOCAL_MACHINE ? L"HKEY_LOCAL_MACHINE" :
            edit.root == HKEY_USERS ? L"HKEY_USERS" : L"HKEY_CURRENT_USER";
        out << L'[' << root << L'\\' << edit.key << L"]\r\n\"" << escaped(edit.name) << L"\"=";
        if (!edit.before.exists) out << L"-";
        else {
            // Preserve the original type and bytes. String values are validated before backup.
            out << L"hex(" << std::hex << edit.before.type << L"):";
            for (std::size_t i = 0; i < edit.before.bytes.size(); ++i) {
                if (i) out << (i % 24 == 0 ? L",\\\r\n  " : L",");
                out << std::setfill(L'0') << std::setw(2) << static_cast<unsigned>(edit.before.bytes[i]);
            }
        }
        out << L"\r\n\r\n";
    }
    return out.str();
}
bool font_file_exists(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
        !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (data.nFileSizeHigh || data.nFileSizeLow);
}
struct Font { const wchar_t* face; const wchar_t* file; bool required; };
constexpr Font defaults[]{
    {L"Segoe UI", L"segoeui.ttf", true},
    {L"Segoe UI Bold", L"segoeuib.ttf", true},
    {L"Segoe UI Italic", L"segoeuii.ttf", true},
    {L"Segoe UI Bold Italic", L"segoeuiz.ttf", true},
    {L"Segoe UI Light", L"segoeuil.ttf", false},
    {L"Segoe UI Light Italic", L"seguili.ttf", false},
    {L"Segoe UI Semibold", L"seguisb.ttf", false},
    {L"Segoe UI Semibold Italic", L"seguisbi.ttf", false},
    {L"Segoe UI Semilight", L"segoeuisl.ttf", false},
    {L"Segoe UI Semilight Italic", L"seguisli.ttf", false},
    {L"Segoe UI Variable", L"SegUIVar.ttf", false}
};
}

Result restore_fonts_at(const FontRecoveryTarget& target) {
    std::vector<Edit> edits;
    std::vector<std::size_t> applied;
    bool backup_saved = false;
    try {
        if (target.dpi < 48 || target.dpi > 768 || target.backup_file.empty())
            return {false, L"Некорректные параметры восстановления шрифтов. Изменений нет."};
        // Preflight every required file before even saving the registry plan.
        for (const auto& font : defaults) if (font.required && !font_file_exists(target.fonts_directory + L"\\" + font.file))
            return {false, L"Отсутствует или пуст файл " + target.fonts_directory + L"\\" + font.file +
                L". Настройки не изменены. Откройте Инструменты → Система: проверка SFC, затем восстановление SFC/DISM. Шрифты не скачиваются из сети."};
        auto add = [&](HKEY root, const std::wstring& key, const std::wstring& name, Value after) {
            edits.push_back({root, key, name, read(root, key, name), std::move(after)});
        };
        for (const auto& font : defaults) {
            if (!font_file_exists(target.fonts_directory + L"\\" + font.file)) continue;
            const auto registration = std::wstring(font.face) + L" (TrueType)";
            add(target.machine_root, target.machine_base + L"\\Fonts", registration, string_value(font.file));
            add(target.user_root, target.user_base + L"\\Fonts", registration, {});
            add(target.machine_root, target.machine_base + L"\\FontSubstitutes", font.face, {});
            add(target.user_root, target.user_base + L"\\FontSubstitutes", font.face, {});
            if (std::wstring(font.face) == L"Segoe UI Variable") for (const auto face :
                {L"Segoe UI Variable Text", L"Segoe UI Variable Small", L"Segoe UI Variable Display"}) {
                add(target.machine_root, target.machine_base + L"\\FontSubstitutes", face, {});
                add(target.user_root, target.user_base + L"\\FontSubstitutes", face, {});
            }
        }
        static_assert(sizeof(LOGFONTW) == 92, "Registry WindowMetrics LOGFONT layout");
        LOGFONTW font{};
        font.lfHeight = -MulDiv(9, static_cast<int>(target.dpi), 72);
        font.lfWeight = FW_NORMAL; font.lfCharSet = DEFAULT_CHARSET; font.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(font.lfFaceName, L"Segoe UI");
        for (const auto name : {L"CaptionFont", L"SmCaptionFont", L"MenuFont", L"StatusFont", L"MessageFont", L"IconFont"})
            add(target.user_root, target.metrics_key, name, raw(REG_BINARY, &font, sizeof(font)));
        // reg.exe normalizes unterminated/odd-length strings during import. Refuse these
        // rather than promising a lossless .reg rollback that Windows cannot provide.
        for (const auto& edit : edits) if (edit.before.exists) {
            const auto& value = edit.before;
            if (value.type == REG_SZ || value.type == REG_EXPAND_SZ || value.type == REG_MULTI_SZ) {
                const auto length = value.bytes.size();
                const std::size_t minimum = value.type == REG_MULTI_SZ ? 4 : 2;
                if (length < minimum || length % 2 ||
                    std::any_of(value.bytes.end() - minimum, value.bytes.end(), [](BYTE b) { return b != 0; }))
                    return {false, L"Некорректный формат исходного строкового значения " + edit.name +
                        L". Windows меняет такие данные при импорте .reg, поэтому точный откат не гарантирован. Настройки не изменены. Требуется ручная диагностика реестра."};
            }
            if ((value.type == REG_DWORD && value.bytes.size() != 4) || (value.type == REG_QWORD && value.bytes.size() != 8))
                return {false, L"Некорректный размер исходного числового значения. Настройки не изменены: точный откат через .reg не гарантирован."};
        }
        applied.reserve(edits.size());
        if (!write_unicode_file(target.backup_file, registry_backup(edits), true))
            return {false, L"Резервная копия не сохранена. Настройки шрифтов не изменены."};
        backup_saved = true;
        // Disk I/O and UAC can take time. Reject stale observations instead of overwriting them.
        for (const auto& edit : edits) if (!(read(edit.root, edit.key, edit.name) == edit.before))
            throw std::runtime_error("Registry changed before repair");
        for (std::size_t i = 0; i < edits.size(); ++i) {
            const auto& edit = edits[i];
            if (edit.before == edit.after) continue;
            if (!(read(edit.root, edit.key, edit.name) == edit.before)) throw std::runtime_error("Concurrent font change");
            write(edit, edit.after);
            applied.push_back(i);
            if (!(read(edit.root, edit.key, edit.name) == edit.after)) throw std::runtime_error("Font verification failed");
        }
        // Detect a setting reapplied by policy or another process, including earlier entries.
        for (const auto& edit : edits) if (!(read(edit.root, edit.key, edit.name) == edit.after))
            throw std::runtime_error("Font settings were reapplied");
        return {true, L"Настройки стандартных шрифтов Segoe UI восстановлены и проверены. Изменено значений: " +
            std::to_wstring(applied.size()) + L".\r\n\r\nСохраните работу и выйдите из учётной записи Windows, затем войдите снова. "
            L"Автоматического выхода или перезагрузки нет. Целостность файлов шрифтов этим не проверена: при сохранении проблемы используйте SFC/DISM.\r\n\r\nКопия для отката: " + target.backup_file};
    } catch (const std::exception&) {
        bool rolled_back = true;
        for (auto i = applied.rbegin(); i != applied.rend(); ++i) {
            const auto& edit = edits[*i];
            try {
                const auto current = read(edit.root, edit.key, edit.name);
                if (current == edit.after) write(edit, edit.before);
                else if (!(current == edit.before)) rolled_back = false;
                if (!(read(edit.root, edit.key, edit.name) == edit.before)) rolled_back = false;
            } catch (const std::exception&) { rolled_back = false; }
        }
        return {false, L"Восстановление шрифтов не завершено: нет доступа, настройка изменилась или проверка не прошла. " +
            std::wstring(applied.empty() ? L"Изменений не выполнено." : rolled_back ? L"Выполненные изменения отменены." :
                L"Часть изменений не удалось отменить. Проверьте резервную копию и права доступа.") +
            (backup_saved ? L"\r\nКопия: " + target.backup_file : L" Резервная копия не создана.")};
    }
}

std::wstring current_user_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    const bool good = size && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    std::wstring result;
    if (good) {
        LPWSTR sid = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) {
            result = sid; LocalFree(sid);
        }
    }
    CloseHandle(token);
    return result;
}
Result restore_system_fonts(const std::wstring& user_sid) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(user_sid.c_str(), &sid)) return {false, L"Не удалось определить исходного пользователя."};
    const bool valid = IsValidSid(sid) != FALSE;
    LocalFree(sid);
    if (!valid || user_sid.find_first_not_of(L"S-0123456789") != std::wstring::npos)
        return {false, L"Некорректный SID исходного пользователя."};
    // Keep targeting the original user's loaded hive after alternate-account UAC.
    Key user;
    if (RegOpenKeyExW(HKEY_USERS, user_sid.c_str(), 0, KEY_READ, &user.value) != ERROR_SUCCESS)
        return {false, L"Профиль исходного пользователя не загружен. Шрифты не изменены."};
    PWSTR profile = nullptr;
    // The backup belongs to the elevated operation; its exact location is shown in the result.
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &profile)))
        return {false, L"Не удалось найти папку резервных копий."};
    const auto directory = std::wstring(profile) + L"\\CrimeSQL dev system recovery\\Backups";
    CoTaskMemFree(profile);
    const int made = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    if (made != ERROR_SUCCESS && made != ERROR_ALREADY_EXISTS) return {false, L"Не удалось создать папку копий."};
    GUID id{}; wchar_t name[40]{}; wchar_t windows[MAX_PATH]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, name, 40) || !GetWindowsDirectoryW(windows, MAX_PATH))
        return {false, L"Не удалось подготовить восстановление шрифтов."};
    constexpr auto base = L"Software\\Microsoft\\Windows NT\\CurrentVersion";
    auto result = restore_fonts_at({HKEY_LOCAL_MACHINE, base, HKEY_USERS, user_sid + L"\\" + base,
        user_sid + L"\\Control Panel\\Desktop\\WindowMetrics", std::wstring(windows) + L"\\Fonts",
        directory + L"\\Fonts-" + name + L".reg", GetDpiForSystem()});
    if (result.success) {
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
    }
    return result;
}
}
