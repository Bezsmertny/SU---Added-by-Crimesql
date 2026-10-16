#include "recovery_actions.h"
#include <array>
#include <stdexcept>

namespace nh {
const ActionInfo& action_info(RecoveryAction action) {
    static const std::array<ActionInfo,16> actions{{
        {L"Безопасность Windows",L"Защита и история обнаружений Microsoft Defender.",L""},
        {L"Быстрая проверка Defender",L"Проверка основных мест, где могут находиться угрозы.",L"Запустить быструю проверку Microsoft Defender? Windows запросит права администратора."},
        {L"Полная проверка Defender",L"Проверка всех доступных файлов; может занять долгое время.",L"Запустить полную проверку Microsoft Defender? Она может занять длительное время."},
        {L"Автономная проверка Defender",L"Проверка вне обычной Windows. Компьютер перезагрузится.",L"Компьютер будет перезагружен для автономной проверки Microsoft Defender. Сохраните работу. Если включён BitLocker, подготовьте ключ восстановления. Нужен настроенный WinRE. Продолжить?"},
        {L"SFC: проверить системные файлы",L"Проверка целостности /verifyonly, без исправления файлов.",L"Запустить проверку целостности системных файлов SFC без исправления?"},
        {L"SFC: восстановить системные файлы",L"Штатное исправление повреждений через /scannow.",L"SFC может заменить повреждённые системные файлы. Это не удаление вирусов. Сохраните работу. Запустить восстановление?"},
        {L"DISM: восстановить образ Windows",L"RestoreHealth; может использовать Windows Update и сеть.",L"DISM может восстановить хранилище компонентов Windows и использовать Windows Update. Процесс может занять долгое время. Продолжить?"},
        {L"Точки восстановления",L"Открыть штатный мастер. Выбор точки выполняется вручную.",L""},
        {L"PowerShell от администратора",L"Обычная Windows, чистый сеанс без загрузки профиля.",L"Открыть PowerShell с повышенными правами? Это консоль текущей Windows, не WinRE. Выполняйте только понятные вам команды."},
        {L"CMD от администратора",L"Командная строка без выполнения команд AutoRun (/d).",L"Открыть командную строку с правами администратора?"},
        {L"Проверить состояние WinRE",L"Показать reagentc /info без изменения конфигурации загрузки.",L"Открыть сведения о среде восстановления Windows? Будет выполнено reagentc /info без изменения настроек."},
        {L"WinRE и безопасный режим",L"Открыть параметры восстановления; перезапуск выбираете вы.",L""},
        {L"Аудит автозагрузки",L"Отчёт HKCU/HKLM Run и RunOnce. Ничего не удаляется.",L""},
        {L"Управление автозагрузкой",L"Открыть настройки автозапуска Windows.",L""},
        {L"Сведения о системе",L"Штатный просмотр оборудования и компонентов Windows.",L""},
        {L"Экспорт диагностики",L"Сохранить состояние проверок и службы Defender в локальный отчёт.",L""}
    }};
    const auto index=static_cast<std::size_t>(action);
    if(index>=actions.size()) throw std::out_of_range("Unknown recovery action");
    return actions[index];
}
std::wstring powershell_quote(const std::wstring& value) {
    std::wstring out=L"'";
    for(auto character:value) { out+=character; if(character==L'\'') out+=L'\''; }
    return out+L"'";
}
std::wstring recovery_script(RecoveryAction action,const std::wstring& system_directory) {
    const auto sfc=powershell_quote(system_directory+L"\\sfc.exe");
    switch(action) {
        case RecoveryAction::defender_quick: return L"Start-MpScan -ScanType QuickScan -ErrorAction Stop; $global:LASTEXITCODE=0";
        case RecoveryAction::defender_full: return L"Start-MpScan -ScanType FullScan -ErrorAction Stop; $global:LASTEXITCODE=0";
        case RecoveryAction::defender_offline: return L"Start-MpWDOScan -ErrorAction Stop; $global:LASTEXITCODE=0";
        case RecoveryAction::sfc_verify: return L"& "+sfc+L" /verifyonly";
        case RecoveryAction::sfc_repair: return L"& "+sfc+L" /scannow";
        case RecoveryAction::dism_restore: return L"& "+powershell_quote(system_directory+L"\\dism.exe")+L" /Online /Cleanup-Image /RestoreHealth";
        case RecoveryAction::winre_info: return L"& "+powershell_quote(system_directory+L"\\reagentc.exe")+L" /info";
        default: return {};
    }
}
std::wstring maintenance_wrapper(const std::wstring& script,const std::wstring& transcript) {
    return L"$ErrorActionPreference='Stop'; $result=1; $transcript=$false; try { "
        L"Start-Transcript -Path "+powershell_quote(transcript)+L" -ErrorAction Stop | Out-Null; $transcript=$true; "+script+
        L"; $result=$LASTEXITCODE; Write-Host ('Exit code: '+$result) } catch { Write-Host $_ -ForegroundColor Red; $result=1 } "
        L"finally { if ($transcript) { Stop-Transcript | Out-Null } }; Write-Host 'Report: ' "+powershell_quote(transcript)+
        L"; Read-Host 'Press Enter to close'; exit $result";
}
}
