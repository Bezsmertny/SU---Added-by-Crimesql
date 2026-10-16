#include "windows_system.h"
#include "windows_features.h"
#include "file_watcher.h"
#include <algorithm>
#include <cstring>
#include <mutex>
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
    std::wstring path = L"Software\\CrimeSQL dev system recovery\\IntegrationTest-" + std::to_wstring(GetCurrentProcessId())
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

        check(nh::binding_name({MOD_CONTROL|MOD_ALT,VK_F8})==L"Ctrl + Alt + F8", "Readable hotkey label");
        const DWORD original_priority=GetPriorityClass(GetCurrentProcess());
        const auto raised=nh::set_high_priority(true);
        const bool high=GetPriorityClass(GetCurrentProcess())==HIGH_PRIORITY_CLASS;
        const bool restored_priority=SetPriorityClass(GetCurrentProcess(),original_priority)!=FALSE;
        check(raised.success&&high&&restored_priority,"Set high priority for own process and restore it");
        // Exercise actual ReadDirectoryChangesW with harmless files, never real policies or samples.
        wchar_t temporary[MAX_PATH]{}; check(GetTempPathW(MAX_PATH,temporary)!=0,"Temporary directory");
        const auto directory=std::wstring(temporary)+L"CrimeSQL-Watcher-"+std::to_wstring(GetCurrentProcessId());
        check(CreateDirectoryW(directory.c_str(),nullptr)!=FALSE,"Create disposable watcher directory");
        std::mutex event_mutex;
        std::vector<nh::FileEvent> events;
        nh::FileWatcher watcher; DWORD error=0;
        check(watcher.start(directory,[&](nh::FileEvent event) { std::lock_guard<std::mutex> lock(event_mutex); events.push_back(std::move(event)); },error),"Arm real file watcher");
        const auto original=directory+L"\\created.txt", renamed=directory+L"\\renamed.txt";
        HANDLE output=CreateFileW(original.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(output!=INVALID_HANDLE_VALUE,"Create observed file");
        DWORD written=0; const char content[]="harmless test";
        bool write=WriteFile(output,content,sizeof(content),&written,nullptr)!=FALSE;
        CloseHandle(output); check(write&&written==sizeof(content),"Write observed file");
        check(MoveFileW(original.c_str(),renamed.c_str())!=FALSE,"Rename observed file");
        check(DeleteFileW(renamed.c_str())!=FALSE,"Delete observed file");
        bool added=false,removed=false,pair=false;
        const auto deadline=GetTickCount64()+5000;
        do {
            { std::lock_guard<std::mutex> lock(event_mutex);
                for(const auto& event:events) {
                    added=added||(event.kind==nh::FileEvent::Kind::added&&event.path==L"created.txt");
                    removed=removed||(event.kind==nh::FileEvent::Kind::removed&&event.path==L"renamed.txt");
                    pair=pair||(event.kind==nh::FileEvent::Kind::renamed&&event.path==L"created.txt"&&event.destination==L"renamed.txt");
                }
            }
            if(added&&removed&&pair) break;
            Sleep(20);
        } while(GetTickCount64()<deadline);
        watcher.stop();
        const auto config_path=directory+L"\\test.wsb";
        const auto config=nh::sandbox_config(directory);
        check(nh::write_unicode_file(config_path,config,true),"Save sandbox configuration");
        check(!nh::write_unicode_file(config_path,L"overwrite",true),"Exclusive writes do not overwrite an existing configuration");
        HANDLE config_file=CreateFileW(config_path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(config_file!=INVALID_HANDLE_VALUE,"Open saved configuration");
        std::vector<wchar_t> config_data(config.size()+1); DWORD config_received=0;
        const bool config_read=ReadFile(config_file,config_data.data(),static_cast<DWORD>(config_data.size()*sizeof(wchar_t)),&config_received,nullptr)!=FALSE;
        CloseHandle(config_file); DeleteFileW(config_path.c_str());
        check(config_read&&config_received==config_data.size()*sizeof(wchar_t)&&std::wstring(config_data.begin(),config_data.end())==L"\xFEFF"+config,
              "Saved sandbox configuration has complete UTF-16 BOM and contents");
        RemoveDirectoryW(directory.c_str());
        if (!added || !removed || !pair) {
            for (const auto& event : events)
                std::cerr << "Observed kind=" << static_cast<int>(event.kind) << " " << std::string(event.path.begin(), event.path.end())
                    << " -> " << std::string(event.destination.begin(), event.destination.end()) << '\n';
        }
        check(added,"Actual file creation event"); check(removed,"Actual file deletion event"); check(pair,"Actual paired rename event");
        nh::RenameTracker decoder;
        std::vector<unsigned char> bad(12); DWORD impossible=0xffffffff;
        std::memcpy(bad.data()+8,&impossible,4);
        auto malformed=nh::decode_notifications(bad.data(),bad.size(),decoder);
        check(malformed.size()==1&&malformed[0].kind==nh::FileEvent::Kind::lost,"Reject malformed notification length");
        // Some providers pack entries without DWORD padding. memcpy decoding must support them.
        const std::wstring short_name=L"created.txt";
        const DWORD name_bytes=static_cast<DWORD>(short_name.size()*sizeof(wchar_t)), stride=12+name_bytes;
        std::vector<unsigned char> packed(stride*2);
        DWORD added_action=1,removed_action=2;
        std::memcpy(packed.data(),&stride,4); std::memcpy(packed.data()+4,&added_action,4);
        std::memcpy(packed.data()+8,&name_bytes,4); std::memcpy(packed.data()+12,short_name.data(),name_bytes);
        std::memcpy(packed.data()+stride+4,&removed_action,4); std::memcpy(packed.data()+stride+8,&name_bytes,4);
        std::memcpy(packed.data()+stride+12,short_name.data(),name_bytes);
        auto unpacked=nh::decode_notifications(packed.data(),packed.size(),decoder);
        check(unpacked.size()==2&&unpacked[0].kind==nh::FileEvent::Kind::added&&unpacked[1].kind==nh::FileEvent::Kind::removed,
              "Decode tightly packed notifications without unaligned reads");
        std::cout << checks << " Windows integration checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Check " << checks << " failed: " << error.what() << '\n';
        return 1;
    }
}
