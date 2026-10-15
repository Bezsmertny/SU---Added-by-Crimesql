#include "windows_system.h"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace {
constexpr COLORREF bg = RGB(11, 16, 23), sidebar = RGB(15, 22, 31);
constexpr COLORREF surface = RGB(20, 29, 40), border = RGB(36, 49, 64);
constexpr COLORREF white = RGB(231, 239, 247), muted = RGB(139, 157, 176);
constexpr COLORREF mint = RGB(84, 231, 181), amber = RGB(255, 199, 106);
constexpr COLORREF active = RGB(24, 54, 50);
constexpr int nav_base = 100, scan_id = 200, repair_base = 300, tool_base = 400, copy_id = 500;
HWND window_handle = nullptr;
std::array<HWND, 4> nav{};
std::array<HWND, 4> repairs{};
std::array<HWND, 5> tools{};
HWND scan_button = nullptr, copy_button = nullptr;
std::array<nh::Observation, 4> observations{};
std::vector<std::wstring> journal;
std::wstring machine, defender;
bool scanned = false;
int page = 0;
double dpi_scale = 1.0;

double scale_for_monitor(UINT dpi, HMONITOR monitor) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    const double preferred = dpi / 96.0;
    if (!GetMonitorInfoW(monitor, &info)) return preferred;
    return std::min({preferred, (info.rcWork.right - info.rcWork.left) / 1180.0,
                     (info.rcWork.bottom - info.rcWork.top) / 850.0});
}

int px(int value) { return static_cast<int>(value * dpi_scale + 0.5); }
int logical(int value) { return static_cast<int>(value / dpi_scale); }
void log(const std::wstring& message) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[24]{};
    swprintf_s(stamp, L"%02u:%02u:%02u  ", now.wHour, now.wMinute, now.wSecond);
    journal.push_back(stamp + message);
}

void rounded(HDC dc, int x, int y, int w, int h, COLORREF fill, COLORREF outline = border, int radius = 14) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, outline);
    auto old_brush = SelectObject(dc, brush);
    auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, px(x), px(y), px(x + w), px(y + h), px(radius), px(radius));
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void text(HDC dc, const std::wstring& value, int x, int y, int w, int h,
          int size = 15, COLORREF color = white, int weight = FW_NORMAL, bool wrap = false) {
    HFONT font = CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT rect{px(x), px(y), px(x + w), px(y + h)};
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect,
              DT_NOPREFIX | (wrap ? DT_WORDBREAK : DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER));
    SelectObject(dc, old);
    DeleteObject(font);
}

std::wstring status(std::size_t index) {
    if (!scanned) return L"Ожидает проверки";
    switch (nh::classify(nh::policies[index], observations[index])) {
        case nh::Health::clear: return L"Ограничений не найдено";
        case nh::Health::blocked: return L"Обнаружена блокировка";
        default:
            if (observations[index].state == nh::ReadState::denied) return L"Нет доступа к настройке";
            if (observations[index].state == nh::ReadState::unsupported_type || observations[index].state == nh::ReadState::dword)
                return L"Неизвестное значение";
            return L"Ошибка чтения настройки";
    }
}

void layout() {
    RECT client{};
    GetClientRect(window_handle, &client);
    const int width = logical(client.right), height = logical(client.bottom);
    const int x = 262, content = width - x - 32;
    for (int i = 0; i < 4; ++i) {
        MoveWindow(nav[i], px(16), px(176 + i * 55), px(198), px(44), TRUE);
        InvalidateRect(nav[i], nullptr, FALSE);
    }
    const bool diagnostic_page = page == 0 || page == 1;
    ShowWindow(scan_button, diagnostic_page ? SW_SHOW : SW_HIDE);
    MoveWindow(scan_button, px(x + content - 188), px(48), px(188), px(44), TRUE);
    for (int i = 0; i < 4; ++i) {
        ShowWindow(repairs[i], diagnostic_page ? SW_SHOW : SW_HIDE);
        MoveWindow(repairs[i], px(x + content - 158), px(346 + i * 77), px(138), px(34), TRUE);
        SetWindowTextW(repairs[i], page == 0 ? L"Подробнее" : L"Восстановить");
        EnableWindow(repairs[i], page == 0 || (scanned && nh::classify(nh::policies[i], observations[i]) == nh::Health::blocked));
    }
    for (int i = 0; i < 5; ++i) {
        ShowWindow(tools[i], page == 2 ? SW_SHOW : SW_HIDE);
        MoveWindow(tools[i], px(x + content - 150), px(179 + i * 91), px(128), px(36), TRUE);
    }
    ShowWindow(copy_button, page == 3 ? SW_SHOW : SW_HIDE);
    MoveWindow(copy_button, px(x), px(height - 104), px(220), px(40), TRUE);
    InvalidateRect(window_handle, nullptr, FALSE);
}

void scan() {
    for (std::size_t i = 0; i < nh::policies.size(); ++i) observations[i] = nh::read_policy(nh::policies[i]);
    scanned = true;
    defender = nh::defender_status();
    log(L"Диагностика текущего пользователя завершена.");
    for (std::size_t i = 0; i < nh::policies.size(); ++i) {
        auto entry = std::wstring(nh::policies[i].name) + L": " + status(i);
        if (observations[i].error) entry += L" — " + nh::error_text(observations[i].error);
        log(entry);
    }
    log(L"Microsoft Defender: " + defender + L". Статус службы не является проверкой на вирусы.");
    layout();
}

void paint(HDC dc) {
    RECT client{};
    GetClientRect(window_handle, &client);
    const int width = logical(client.right), height = logical(client.bottom);
    HBRUSH background = CreateSolidBrush(bg);
    FillRect(dc, &client, background);
    DeleteObject(background);
    RECT side{0, 0, px(230), client.bottom};
    HBRUSH side_brush = CreateSolidBrush(sidebar);
    FillRect(dc, &side, side_brush);
    DeleteObject(side_brush);
    rounded(dc, 24, 32, 44, 44, active, active, 12);
    text(dc, L"N/", 33, 37, 38, 34, 24, mint, FW_BOLD);
    text(dc, L"NEDOHACKERS", 79, 31, 145, 26, 16, white, FW_BOLD);
    text(dc, L"lite  /  SYSTEM RECOVERY", 80, 57, 142, 20, 10, muted);
    text(dc, L"РАБОЧЕЕ ПРОСТРАНСТВО", 24, 132, 186, 20, 10, muted, FW_BOLD);
    rounded(dc, 16, height - 146, 198, 80, surface);
    text(dc, L"Локальный режим", 30, height - 133, 170, 23, 13, mint, FW_SEMIBOLD);
    text(dc, L"Данные остаются на ПК", 30, height - 106, 170, 22, 11, muted);
    text(dc, L"v0.1.0  /  COMMUNITY EDITION", 24, height - 42, 194, 20, 10, muted);

    const int x = 262, content = width - x - 32;
    const wchar_t* headings[]{L"Центр восстановления", L"Восстановление доступа", L"Инструменты Windows", L"Журнал действий"};
    const wchar_t* subtitles[]{L"Проверь настройки. Верни контроль над системой.", L"Точечные изменения с резервной копией перед каждым действием.",
        L"Штатные средства диагностики, защиты и восстановления.", L"Локальная история текущего сеанса. Без отправки данных."};
    text(dc, L"NEDOHACKERS LITE  /  SYSTEM UNLOCKER", x, 19, content, 20, 10, mint, FW_BOLD);
    text(dc, headings[page], x, 53, content - (page < 2 ? 200 : 0), 42, 28, white, FW_BOLD);
    text(dc, subtitles[page], x, 102, content, 28, 14, muted);

    if (page < 2) {
        const int gap = 14, card = (content - gap * 2) / 3;
        int blocked = 0, unknown = 0;
        for (std::size_t i = 0; i < observations.size() && scanned; ++i) {
            auto health = nh::classify(nh::policies[i], observations[i]);
            blocked += health == nh::Health::blocked;
            unknown += health == nh::Health::unknown;
        }
        for (int i = 0; i < 3; ++i) rounded(dc, x + i * (card + gap), 154, card, 124, surface);
        text(dc, L"ОГРАНИЧЕНИЯ ПРОФИЛЯ", x + 18, 169, card - 30, 22, 10, muted, FW_BOLD);
        text(dc, scanned ? std::to_wstring(blocked) : L"—", x + 18, 196, 55, 44, 34, blocked ? amber : mint, FW_BOLD);
        text(dc, scanned ? L"из 4 проверок" : L"ещё не проверено", x + 72, 208, card - 88, 22, 12, muted);
        text(dc, scanned && unknown ? L"Есть непрочитанные настройки" : L"Только текущий пользователь", x + 18, 248, card - 30, 18, 10, muted);
        text(dc, L"MICROSOFT DEFENDER", x + card + gap + 18, 169, card - 30, 22, 10, muted, FW_BOLD);
        text(dc, scanned ? defender : L"Ожидает проверки", x + card + gap + 18, 202, card - 30, 28, 16, white, FW_SEMIBOLD);
        text(dc, L"Информация о службе WinDefend", x + card + gap + 18, 248, card - 30, 18, 10, muted);
        text(dc, L"ЭТОТ КОМПЬЮТЕР", x + 2 * (card + gap) + 18, 169, card - 30, 22, 10, muted, FW_BOLD);
        text(dc, machine, x + 2 * (card + gap) + 18, 202, card - 30, 28, 16, white, FW_SEMIBOLD);
        text(dc, L"Без автоматических изменений", x + 2 * (card + gap) + 18, 248, card - 30, 18, 10, muted);
        text(dc, page == 0 ? L"Системные ограничения" : L"Выберите настройку для восстановления", x, 296, content, 28, 18, white, FW_SEMIBOLD);
        for (std::size_t i = 0; i < nh::policies.size(); ++i) {
            const int y = 331 + static_cast<int>(i) * 77;
            rounded(dc, x, y, content, 68, surface);
            text(dc, L"0" + std::to_wstring(i + 1), x + 18, y + 16, 30, 32, 14, muted, FW_SEMIBOLD);
            text(dc, nh::policies[i].name, x + 62, y + 8, content - 238, 26, 16, white, FW_SEMIBOLD);
            const auto health = nh::classify(nh::policies[i], observations[i]);
            text(dc, status(i), x + 62, y + 35, content - 238, 22, 12,
                 !scanned ? muted : health == nh::Health::clear ? mint : amber);
        }
        text(dc, L"Рабочий или учебный ПК? Ограничения могут быть установлены администратором.", x, 649, content, 26, 12, amber);
        text(dc, page == 0 ? L"Проверка политик не определяет наличие вирусов. Для этого открой «Безопасность Windows»."
             : L"Перед изменением создаётся копия .reg. Найти её можно в разделе «Инструменты → Резервные копии».", x, 680, content, 40, 12, muted, FW_NORMAL, true);
    } else if (page == 2) {
        const wchar_t* names[]{L"Безопасность Windows", L"Диспетчер задач", L"Сведения о системе", L"Восстановление Windows", L"Резервные копии"};
        const wchar_t* descriptions[]{L"Проверка на вирусы через установленное средство защиты.", L"Процессы, автозагрузка и использование ресурсов.",
            L"Информация об оборудовании и компонентах Windows.", L"Открыть параметры восстановления системы.", L"Исходные значения настроек в формате .reg."};
        for (int i = 0; i < 5; ++i) {
            const int y = 156 + i * 91;
            rounded(dc, x, y, content, 78, surface);
            text(dc, names[i], x + 20, y + 12, content - 190, 26, 17, white, FW_SEMIBOLD);
            text(dc, descriptions[i], x + 20, y + 43, content - 190, 24, 12, muted);
        }
        text(dc, L"Системные инструменты могут запросить права администратора.", x, 641, content, 30, 13, muted);
    } else {
        rounded(dc, x, 154, content, height - 279, surface);
        const int capacity = std::max(1, (height - 320) / 48);
        const std::size_t first = journal.size() > static_cast<std::size_t>(capacity) ? journal.size() - capacity : 0;
        int y = 170;
        for (std::size_t i = first; i < journal.size(); ++i, y += 48)
            text(dc, journal[i], x + 18, y, content - 36, 43, 12, muted, FW_NORMAL, true);
        text(dc, L"Показаны последние записи. Кнопка копирует весь журнал, включая пути к копиям.", x + 240, height - 103, content - 240, 44, 11, muted, FW_NORMAL, true);
    }
    text(dc, L"NEDOHACKERS lite  •  Инструмент восстановления Windows 10 / 11", x, height - 38, content, 20, 11, muted);
}

HWND button(int id, const wchar_t* title) {
    return CreateWindowW(L"BUTTON", title, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                         0, 0, 0, 0, window_handle, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

void copy_log() {
    std::wstring output = L"NEDOHACKERS lite 0.1.0\r\n";
    for (const auto& entry : journal) output += entry + L"\r\n";
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (output.size() + 1) * sizeof(wchar_t));
    if (!memory) return;
    void* destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return; }
    memcpy(destination, output.c_str(), (output.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    bool transferred = false;
    if (OpenClipboard(window_handle)) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) transferred = true;
        CloseClipboard();
    }
    if (!transferred) GlobalFree(memory);
    MessageBoxW(window_handle, transferred ? L"Журнал скопирован. Перед отправкой проверьте содержащиеся в нём пути к файлам."
        : L"Не удалось скопировать журнал. Повторите попытку.", L"Журнал", MB_OK | (transferred ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void command(int id) {
    if (id >= nav_base && id < nav_base + 4) { page = id - nav_base; layout(); }
    else if (id == scan_id) scan();
    else if (id >= repair_base && id < repair_base + 4) {
        if (page == 0) { page = 1; layout(); return; }
        const auto index = static_cast<std::size_t>(id - repair_base);
        if (!scanned || nh::classify(nh::policies[index], observations[index]) != nh::Health::blocked) return;
        const auto& policy = nh::policies[index];
        std::wstring question = std::wstring(L"Восстановить доступ: ") + policy.name + L"?\n\nБудет удалено только значение текущего пользователя:\nHKCU\\"
            + policy.key + L"\\" + policy.value + L"\nТекущее значение DWORD: " + std::to_wstring(observations[index].value)
            + L"\n\nИсходное значение будет сохранено в .reg в папке:\n" + nh::backup_directory()
            + L"\n\nНа рабочем или учебном ПК сначала обратитесь к администратору. Ограничение само по себе не доказывает заражение."
              L"\n\nПродолжить?";
        if (MessageBoxW(window_handle, question.c_str(), L"Подтверждение восстановления", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        auto result = nh::restore_policy(policy, observations[index]);
        log(std::wstring(policy.name) + L": " + result.message);
        MessageBoxW(window_handle, result.message.c_str(), result.success ? L"Настройка восстановлена" : L"Восстановление не выполнено",
                    MB_OK | (result.success ? MB_ICONINFORMATION : MB_ICONWARNING));
        scan();
    } else if (id >= tool_base && id < tool_base + 5) {
        std::wstring error;
        const bool opened = nh::open_tool(window_handle, static_cast<nh::Tool>(id - tool_base), error);
        const wchar_t* names[]{L"Безопасность Windows", L"Диспетчер задач", L"Сведения о системе", L"Восстановление Windows", L"Резервные копии"};
        log(opened ? std::wstring(L"Запрос открытия: ") + names[id - tool_base] : std::wstring(names[id - tool_base]) + L": " + error);
        if (!opened) MessageBoxW(window_handle, error.c_str(), L"Инструмент недоступен", MB_OK | MB_ICONWARNING);
    } else if (id == copy_id) copy_log();
    InvalidateRect(window_handle, nullptr, FALSE);
}

LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            window_handle = hwnd;
            dpi_scale = scale_for_monitor(GetDpiForWindow(hwnd), MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
            nav[0] = button(nav_base, L"01    Обзор системы");
            nav[1] = button(nav_base + 1, L"02    Восстановление");
            nav[2] = button(nav_base + 2, L"03    Инструменты");
            nav[3] = button(nav_base + 3, L"04    Журнал действий");
            scan_button = button(scan_id, L"Проверить систему");
            for (int i = 0; i < 4; ++i) repairs[i] = button(repair_base + i, L"Восстановить");
            for (int i = 0; i < 5; ++i) tools[i] = button(tool_base + i, L"Открыть");
            copy_button = button(copy_id, L"Копировать весь журнал");
            machine = nh::system_summary();
            log(L"Приложение запущено. Изменения настроек не выполнялись.");
            layout();
            return 0;
        case WM_SIZE: if (window_handle) layout(); return 0;
        case WM_DPICHANGED: {
            dpi_scale = scale_for_monitor(HIWORD(wparam), MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
            auto rectangle = reinterpret_cast<RECT*>(lparam);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            const int width = std::min(static_cast<int>(monitor.rcWork.right - monitor.rcWork.left),
                std::max(px(1110), static_cast<int>(rectangle->right - rectangle->left)));
            const int height = std::min(static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top),
                std::max(px(820), static_cast<int>(rectangle->bottom - rectangle->top)));
            SetWindowPos(hwnd, nullptr, rectangle->left, rectangle->top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = {px(1110), px(820)};
            return 0;
        }
        case WM_COMMAND: if (HIWORD(wparam) == BN_CLICKED) command(LOWORD(wparam)); return 0;
        case WM_DRAWITEM: {
            auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            const int id = static_cast<int>(draw->CtlID);
            const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
            const bool selected = id >= nav_base && id < nav_base + 4 && id - nav_base == page;
            const bool primary = id == scan_id;
            HBRUSH behind = CreateSolidBrush(id < scan_id ? sidebar : id == scan_id || id == copy_id ? bg : surface);
            FillRect(draw->hDC, &draw->rcItem, behind);
            DeleteObject(behind);
            const int x = logical(draw->rcItem.left), y = logical(draw->rcItem.top);
            const int w = logical(draw->rcItem.right - draw->rcItem.left), h = logical(draw->rcItem.bottom - draw->rcItem.top);
            COLORREF fill = primary ? mint : selected ? active : surface;
            if (draw->itemState & ODS_SELECTED) fill = selected ? surface : active;
            rounded(draw->hDC, x, y, w, h, fill, selected ? mint : fill, 10);
            wchar_t title[128]{};
            GetWindowTextW(draw->hwndItem, title, 128);
            text(draw->hDC, title, x + 14, y, w - 22, h, id < scan_id ? 13 : 12,
                 disabled ? RGB(81, 99, 115) : primary ? bg : selected ? mint : white, FW_SEMIBOLD);
            if (draw->itemState & ODS_FOCUS) {
                RECT focus = draw->rcItem;
                InflateRect(&focus, -px(4), -px(4));
                DrawFocusRect(draw->hDC, &focus);
            }
            return TRUE;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            if (client.right > 0 && client.bottom > 0) {
                HDC buffer = CreateCompatibleDC(dc);
                HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
                auto old = SelectObject(buffer, bitmap);
                paint(buffer);
                BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
                SelectObject(buffer, old);
                DeleteObject(bitmap);
                DeleteDC(buffer);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    POINT origin{};
    dpi_scale = scale_for_monitor(GetDpiForSystem(), MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"NedohackersLiteRecovery";
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_SHIELD);
    if (!RegisterClassW(&window_class)) { if (SUCCEEDED(com)) CoUninitialize(); return 1; }
    HWND hwnd = CreateWindowExW(0, window_class.lpszClassName, L"NEDOHACKERS lite — System Recovery",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, px(1180), px(850),
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) { if (SUCCEEDED(com)) CoUninitialize(); return 1; }
    ShowWindow(hwnd, show);
    MSG message{};
    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
