#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks=0;
void check(bool condition,const char* message) { ++checks; if(!condition) throw std::runtime_error(message); }
struct Application {
    PROCESS_INFORMATION process{};
    HWND window=nullptr;
    ~Application() {
        if(window) PostMessageW(window,WM_CLOSE,0,0);
        if(process.hProcess) {
            if(WaitForSingleObject(process.hProcess,5000)==WAIT_TIMEOUT) TerminateProcess(process.hProcess,1);
            CloseHandle(process.hProcess);
        }
        if(process.hThread) CloseHandle(process.hThread);
    }
};
struct Search { DWORD process; HWND window; };
BOOL CALLBACK find_window(HWND window,LPARAM parameter) {
    auto& search=*reinterpret_cast<Search*>(parameter); DWORD process=0;
    GetWindowThreadProcessId(window,&process);
    wchar_t type[128]{}; GetClassNameW(window,type,128);
    if(process==search.process&&std::wstring(type)==L"CrimeSQLRecovery") search.window=window;
    return TRUE;
}
BOOL CALLBACK find_dialog(HWND window,LPARAM parameter) {
    auto& search=*reinterpret_cast<Search*>(parameter); DWORD process=0;
    GetWindowThreadProcessId(window,&process);
    wchar_t type[128]{}; GetClassNameW(window,type,128);
    if(process==search.process&&std::wstring(type)==L"#32770"&&IsWindowVisible(window)) search.window=window;
    return TRUE;
}
void send(HWND window,UINT message,WPARAM w=0,LPARAM l=0) {
    DWORD_PTR result=0;
    check(SendMessageTimeoutW(window,message,w,l,SMTO_ABORTIFHUNG,5000,&result)!=0,"UI message completed without hanging");
}
void inspect_button(HWND button) {
    RECT r{}; GetClientRect(button,&r); check(r.right>0&&r.bottom>0,"Visible control has positive dimensions");
    DWORD_PTR pixels=0;
    check(SendMessageTimeoutW(button,WM_APP+41,0,0,SMTO_ABORTIFHUNG,5000,&pixels)!=0,"In-process renderer inspection completed");
    check((pixels&0xff00)==0xc500,"UI test renderer bridge is available");
    check((pixels&1)==0,"Background erase must not paint a white rectangle");
    check((pixels&2)==0,"Button paints its complete client area");
    check((pixels&4)==0,"Button stays dark or accented instead of solid white");
    check((pixels&8)==0,"Displayed control is not a solid white rectangle");
    check((pixels&0x80)==0,"Renderer test bitmap allocated");
    send(button,BM_SETSTATE,TRUE); send(button,BM_SETSTATE,FALSE);
    send(button,WM_MOUSEMOVE,0,MAKELPARAM(5,5)); send(button,WM_MOUSELEAVE);
    RedrawWindow(button,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    check(SendMessageTimeoutW(button,WM_APP+41,0,0,SMTO_ABORTIFHUNG,5000,&pixels)!=0&&pixels==0xc500,
        "Hover, press and release retain fully painted controls");
}
std::string callback_error;
BOOL CALLBACK inspect_child(HWND child,LPARAM) {
    try { if(IsWindowVisible(child)) inspect_button(child); }
    catch(const std::exception& error) { callback_error=error.what(); return FALSE; }
    return TRUE;
}
void inspect_children(HWND window) {
    callback_error.clear(); EnumChildWindows(window,inspect_child,0);
    if(!callback_error.empty()) throw std::runtime_error(callback_error);
}
std::uint64_t cpu_time(HANDLE process) {
    FILETIME created{},exit{},kernel{},user{}; check(GetProcessTimes(process,&created,&exit,&kernel,&user)!=FALSE,"Read process CPU time");
    ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return k.QuadPart+u.QuadPart;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==2,"Application path supplied"); Application app; STARTUPINFOW startup{}; startup.cb=sizeof(startup);
        std::wstring command=L"\""+std::wstring(argv[1])+L"\" --ui-test";
        check(CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&app.process)!=FALSE,"Launch isolated UI test process");
        WaitForInputIdle(app.process.hProcess,10000); Search search{app.process.dwProcessId,nullptr};
        for(int i=0;i<100&&!search.window;++i) { EnumWindows(find_window,reinterpret_cast<LPARAM>(&search)); if(!search.window) Sleep(50); }
        app.window=search.window; check(app.window!=nullptr,"Find application window");
        for(int page=0;page<7;++page) {
            send(app.window,WM_COMMAND,100+page); inspect_children(app.window);
            if(page==5) for(int game=0;game<6;++game) {
                send(app.window,WM_COMMAND,500+game); inspect_children(app.window);
                if(game!=1) send(app.window,WM_COMMAND,300);
            }
            if(page==3) for(int category=0;category<5;++category) {
                send(app.window,WM_COMMAND,600+category); inspect_children(app.window);
                if(category==4) {
                    check(PostMessageW(app.window,WM_COMMAND,716,0)!=FALSE,"Request font-recovery confirmation only");
                    Search dialog{app.process.dwProcessId,nullptr};
                    for(int i=0;i<100&&!dialog.window;++i) { EnumWindows(find_dialog,reinterpret_cast<LPARAM>(&dialog)); if(!dialog.window) Sleep(20); }
                    check(dialog.window!=nullptr,"Font recovery opens confirmation before any repair");
                    const auto no=GetDlgItem(dialog.window,IDNO);
                    check(no&&(GetWindowLongPtrW(no,GWL_STYLE)&BS_TYPEMASK)==BS_DEFPUSHBUTTON,"Font-recovery confirmation defaults to No");
                    send(dialog.window,WM_COMMAND,IDNO,reinterpret_cast<LPARAM>(GetDlgItem(dialog.window,IDNO)));
                    send(app.window,WM_NULL);
                    check(IsWindowEnabled(app.window)!=FALSE,"Declining font repair returns to the application");
                    inspect_children(app.window);
                }
            }
        }
        const DWORD baseline=GetGuiResources(app.process.hProcess,GR_GDIOBJECTS);
        for(int pass=0;pass<12;++pass) for(int page=0;page<7;++page) {
            send(app.window,WM_COMMAND,100+page);
            if(page==5) send(app.window,WM_COMMAND,500+pass%6);
            SetWindowPos(app.window,nullptr,0,0,pass%2?1240:1260,pass%2?860:880,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            inspect_children(app.window);
        }
        const DWORD after=GetGuiResources(app.process.hProcess,GR_GDIOBJECTS);
        const bool wine=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version")!=nullptr;
        if(wine&&baseline==0&&after==0) std::cout<<"GDI resource counter unavailable under Wine; leak check SKIPPED\n";
        else check(baseline>0&&after<=baseline+12,"No GDI handle growth after repeated navigation and resizing");
        ShowWindow(app.window,SW_MINIMIZE); Sleep(100); check(IsIconic(app.window)!=FALSE,"Minimize application");
        ShowWindow(app.window,SW_RESTORE); send(app.window,WM_COMMAND,104); inspect_children(app.window);
        Sleep(350);
        DWORD_PTR interval=999; SendMessageTimeoutW(app.window,WM_APP+42,0,0,SMTO_ABORTIFHUNG,5000,&interval);
        check(interval==0,"Static idle page has no animation polling timer");
        const auto before_cpu=cpu_time(app.process.hProcess); Sleep(600); const auto used=cpu_time(app.process.hProcess)-before_cpu;
        check(used<1500000,"Static page uses under 150 ms of CPU during 600 ms idle interval");
        send(app.window,WM_COMMAND,200); inspect_children(app.window);
        std::cout<<checks<<" UI checks passed; GDI baseline="<<baseline<<", after="<<after<<", idle CPU ms="<<used/10000<<'\n'; return 0;
    } catch(const std::exception& e) { std::cerr<<"UI check "<<checks<<" failed: "<<e.what()<<'\n'; return 1; }
}
