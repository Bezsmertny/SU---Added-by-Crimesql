#include "recovery_features.h"
#include <algorithm>
#include <random>

namespace nh {
bool valid_binding(Binding b) {
    // MOD_ALT/CONTROL/SHIFT only; require Ctrl or Alt to avoid intercepting typing.
    return (b.modifiers & ~7u) == 0 && (b.modifiers & 3u) != 0 &&
        ((b.key >= 'A' && b.key <= 'Z') || (b.key >= '0' && b.key <= '9') || (b.key >= 0x70 && b.key <= 0x87));
}
bool same_binding(Binding a, Binding b) { return a.modifiers == b.modifiers && a.key == b.key; }
std::wstring autorun_command(const std::wstring& executable) {
    if (executable.empty() || executable.find(L'"') != std::wstring::npos || executable.find_first_of(L"\r\n") != std::wstring::npos) return {};
    return L"\"" + executable + L"\" --background";
}
std::wstring xml_escape(const std::wstring& value) {
    std::wstring out;
    for (auto c : value) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default: out += c;
        }
    }
    return out;
}
std::wstring sandbox_config(const std::wstring& input_folder) {
    // No sample path is interpolated into PowerShell. Only fixed paths inside the VM.
    const std::wstring command =
        L"powershell.exe -NoLogo -NoProfile -NonInteractive -Command \""
        L"$ErrorActionPreference='Stop'; "
        L"New-Item -ItemType Directory -Path 'C:\\CrimeSQLLab' -Force | Out-Null; "
        L"Copy-Item 'C:\\CrimeSQLInput\\sample.exe' 'C:\\CrimeSQLLab\\sample.exe'; "
        L"Copy-Item 'C:\\CrimeSQLInput\\CrimeSQLObserver.exe' 'C:\\CrimeSQLLab\\CrimeSQLObserver.exe'; "
        L"Start-Process 'C:\\CrimeSQLLab\\CrimeSQLObserver.exe'; "
        L"$deadline=(Get-Date).AddSeconds(30); "
        L"while (!(Test-Path 'C:\\CrimeSQLLab\\observer.ready')) { "
        L"if ((Get-Date) -gt $deadline) { throw 'Observer did not start. Sample was not launched.' }; Start-Sleep -Milliseconds 100 }; "
        L"Start-Process 'C:\\CrimeSQLLab\\sample.exe' -WorkingDirectory 'C:\\CrimeSQLLab'\"";
    return L"<?xml version=\"1.0\" encoding=\"utf-16\"?>\r\n<Configuration>\r\n"
        L"<VGpu>Disable</VGpu><Networking>Disable</Networking>\r\n"
        L"<AudioInput>Disable</AudioInput><VideoInput>Disable</VideoInput>\r\n"
        L"<ClipboardRedirection>Disable</ClipboardRedirection><PrinterRedirection>Disable</PrinterRedirection>\r\n"
        L"<ProtectedClient>Enable</ProtectedClient><MemoryInMB>4096</MemoryInMB>\r\n"
        L"<MappedFolders><MappedFolder><HostFolder>" + xml_escape(input_folder) +
        L"</HostFolder><SandboxFolder>C:\\CrimeSQLInput</SandboxFolder><ReadOnly>true</ReadOnly>"
        L"</MappedFolder></MappedFolders>\r\n<LogonCommand><Command>" + xml_escape(command) +
        L"</Command></LogonCommand>\r\n</Configuration>\r\n";
}

std::vector<FileEvent> RenameTracker::flush() {
    if (old_name.empty()) return {};
    std::vector<FileEvent> out{{FileEvent::Kind::moved_out, old_name, {}}};
    old_name.clear();
    return out;
}
std::vector<FileEvent> RenameTracker::accept(unsigned action, const std::wstring& name) {
    if (action == 5 && !old_name.empty()) {
        std::vector<FileEvent> out{{FileEvent::Kind::renamed, old_name, name}};
        old_name.clear();
        return out;
    }
    auto out = flush();
    switch (action) {
        case 1: out.push_back({FileEvent::Kind::added, name, {}}); break;
        case 2: out.push_back({FileEvent::Kind::removed, name, {}}); break;
        case 3: out.push_back({FileEvent::Kind::modified, name, {}}); break;
        case 4: old_name = name; break;
        case 5: out.push_back({FileEvent::Kind::moved_in, name, {}}); break;
        default: out.push_back({FileEvent::Kind::lost, L"Неизвестное событие наблюдения", {}}); break;
    }
    return out;
}
const wchar_t* event_name(FileEvent::Kind kind) {
    switch (kind) {
        case FileEvent::Kind::added: return L"Создано";
        case FileEvent::Kind::removed: return L"Удалено";
        case FileEvent::Kind::modified: return L"Изменено";
        case FileEvent::Kind::renamed: return L"Переименовано";
        case FileEvent::Kind::moved_in: return L"Перемещено в область";
        case FileEvent::Kind::moved_out: return L"Перемещено из области";
        default: return L"Потеря событий";
    }
}

void MemoryGame::reset(unsigned seed) {
    for (int i = 0; i < 16; ++i) cards[static_cast<std::size_t>(i)] = i / 2;
    std::mt19937 generator(seed);
    std::shuffle(cards.begin(), cards.end(), generator);
    matched.fill(false); first = second = -1; attempts = pairs = 0;
}
bool MemoryGame::visible(int index) const {
    return index >= 0 && index < 16 && (matched[static_cast<std::size_t>(index)] || index == first || index == second);
}
bool MemoryGame::choose(int index) {
    if (index < 0 || index >= 16 || second >= 0 || visible(index)) return false;
    if (first < 0) { first = index; return true; }
    second = index; ++attempts;
    if (cards[static_cast<std::size_t>(first)] == cards[static_cast<std::size_t>(second)]) {
        matched[static_cast<std::size_t>(first)] = matched[static_cast<std::size_t>(second)] = true;
        ++pairs; first = second = -1;
    }
    return true;
}
void MemoryGame::conceal() { if (second >= 0) first = second = -1; }
}
