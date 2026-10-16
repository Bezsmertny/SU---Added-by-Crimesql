#include "file_watcher.h"
#include "windows_features.h"
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <deque>
#include <mutex>
#include <vector>

namespace {
struct Entry { std::wstring time; nh::FileEvent event; };
std::mutex queue_mutex;
std::deque<Entry> queue;
std::vector<Entry> history;
std::size_t dropped = 0;
HWND list = nullptr, status_label = nullptr, save_button = nullptr;
nh::FileWatcher watcher;
HFONT font = nullptr;
HBRUSH background = nullptr;
std::wstring clock_text() {
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t text[32]{}; swprintf_s(text, L"%02u:%02u:%02u.%03u", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return text;
}
void enqueue(nh::FileEvent event) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    if (queue.size() >= 4096) { ++dropped; return; }
    queue.push_back({clock_text(), std::move(event)});
}
void drain() {
    std::deque<Entry> current;
    std::size_t lost = 0;
    { std::lock_guard<std::mutex> lock(queue_mutex); current.swap(queue); lost = dropped; dropped = 0; }
    if (lost) current.push_back({clock_text(), {nh::FileEvent::Kind::lost, L"Очередь переполнена. Не показано событий: " + std::to_wstring(lost), {}}});
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    for (auto& entry : current) {
        if (history.size() >= 10000) { history.erase(history.begin()); ListView_DeleteItem(list, 0); }
        int row = static_cast<int>(history.size());
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row; item.pszText = entry.time.data();
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, row, 1, const_cast<wchar_t*>(nh::event_name(entry.event.kind)));
        std::wstring path = entry.event.kind == nh::FileEvent::Kind::lost ? entry.event.path : L"C:\\" + entry.event.path;
        auto destination = entry.event.destination.empty() ? std::wstring{} : L"C:\\" + entry.event.destination;
        ListView_SetItemText(list, row, 2, path.data());
        ListView_SetItemText(list, row, 3, destination.data());
        history.push_back(std::move(entry));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, FALSE);
    const auto title = L"Наблюдение C:\\  •  Последних событий: " + std::to_wstring(history.size()) + L" / 10000. При закрытии Sandbox данные удаляются.";
    SetWindowTextW(status_label, title.c_str());
}
void export_report(HWND owner) {
    wchar_t file[32768] = L"C:\\CrimeSQLLab\\events.tsv";
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
    dialog.lpstrFile = file; dialog.nMaxFile = 32768; dialog.lpstrFilter = L"TSV report\0*.tsv\0\0";
    dialog.lpstrDefExt = L"tsv"; dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return;
    auto clean = [](std::wstring value) {
        for (auto& character : value) if (character == L'\t' || character == L'\r' || character == L'\n') character = L' ';
        // Protect spreadsheet consumers against formula interpretation.
        if (!value.empty() && (value[0] == L'=' || value[0] == L'+' || value[0] == L'-' || value[0] == L'@')) value.insert(value.begin(), L'\'');
        return value;
    };
    std::wstring report = L"Time\tAction\tPath\tDestination\r\n";
    for (const auto& entry : history)
        report += entry.time + L"\t" + nh::event_name(entry.event.kind) + L"\t" + clean(L"C:\\" + entry.event.path) + L"\t" + clean(entry.event.destination) + L"\r\n";
    MessageBoxW(owner, nh::write_unicode_file(file, report) ? L"Отчёт сохранён внутри текущей среды. При закрытии Sandbox он удалится."
        : L"Не удалось сохранить отчёт.", L"CrimeSQL Observer", MB_OK);
}
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            auto label = CreateWindowW(L"STATIC", L"CrimeSQL dev system recovery  /  EXE LAB\nСобытия всей Sandbox, включая фоновые процессы. Авторство EXE не устанавливается.",
                WS_CHILD | WS_VISIBLE, 18, 12, 1000, 55, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            status_label = CreateWindowW(L"STATIC", L"Подготовка наблюдения…", WS_CHILD | WS_VISIBLE, 18, 73, 1000, 30, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(status_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                18, 110, 1050, 520, hwnd, nullptr, nullptr, nullptr);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            ListView_SetBkColor(list, RGB(16, 23, 33)); ListView_SetTextBkColor(list, RGB(16, 23, 33)); ListView_SetTextColor(list, RGB(218, 232, 244));
            SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            const wchar_t* titles[]{L"Время", L"Действие", L"Путь", L"Новое имя / путь"};
            int sizes[]{115, 170, 410, 360};
            for (int i = 0; i < 4; ++i) { LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(titles[i]); column.cx = sizes[i]; ListView_InsertColumn(list, i, &column); }
            save_button = CreateWindowW(L"BUTTON", L"Сохранить TSV внутри Sandbox", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 18, 645, 320, 36, hwnd, reinterpret_cast<HMENU>(1), nullptr, nullptr);
            SendMessageW(save_button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            DWORD error = 0;
            if (!watcher.start(L"C:\\", enqueue, error)) {
                MessageBoxW(hwnd, (L"Наблюдение не запущено. Образец запускаться не должен. " + nh::error_text(error)).c_str(), L"Ошибка наблюдения", MB_OK | MB_ICONERROR);
                return -1;
            }
            // Readiness is written only after ReadDirectoryChangesW is armed.
            if (GetFileAttributesW(L"C:\\CrimeSQLLab") != INVALID_FILE_ATTRIBUTES)
                nh::write_unicode_file(L"C:\\CrimeSQLLab\\observer.ready", L"ready", true);
            SetTimer(hwnd, 1, 100, nullptr);
            return 0;
        }
        case WM_TIMER: drain(); return 0;
        case WM_COMMAND: if (LOWORD(wparam) == 1) export_report(hwnd); return 0;
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wparam),RGB(16,23,33)); SetTextColor(reinterpret_cast<HDC>(wparam),RGB(218,232,244));
            return reinterpret_cast<LRESULT>(background);
        case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize={920,560}; return 0;
        case WM_SIZE:
            if (list) MoveWindow(list, 18, 110, LOWORD(lparam) - 36, HIWORD(lparam) - 175, TRUE);
            if (save_button) MoveWindow(save_button,18,HIWORD(lparam)-48,320,36,TRUE);
            return 0;
        case WM_DESTROY: watcher.stop(); if (font) DeleteObject(font); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSW type{}; type.hInstance = instance; type.lpfnWndProc = procedure; type.lpszClassName = L"CrimeSQLObserver";
    background=CreateSolidBrush(RGB(16,23,33));
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hbrBackground = background;
    RegisterClassW(&type);
    HWND window = CreateWindowW(type.lpszClassName, L"CrimeSQL dev system recovery — Sandbox file timeline", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1160, 760, nullptr, nullptr, instance, nullptr);
    if (!window) { watcher.stop(); DeleteObject(background); if (SUCCEEDED(com)) CoUninitialize(); return 1; }
    ShowWindow(window, show); MSG message{}; int code = 0;
    while ((code = GetMessageW(&message, nullptr, 0, 0)) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (SUCCEEDED(com)) CoUninitialize();
    DeleteObject(background);
    return code < 0 ? 1 : static_cast<int>(message.wParam);
}
