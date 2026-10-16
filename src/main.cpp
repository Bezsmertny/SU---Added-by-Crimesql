#include "windows_features.h"
#include "mini_games.h"
#include "font_recovery.h"
#include <objbase.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <map>
#include <set>
#include <vector>

namespace {
constexpr COLORREF bg=RGB(10,13,21), side=RGB(13,17,27), panel=RGB(19,25,38), line=RGB(36,46,65);
constexpr COLORREF ink=RGB(235,241,251), muted=RGB(140,155,179), teal=RGB(85,230,193), violet=RGB(162,141,255), amber=RGB(255,200,117);
constexpr int nav_base=100, scan_id=200, fix_base=210, mouse_id=220, keyboard_id=221, tools_base=230;
constexpr int defender_id=240, lab_id=250, autorun_id=260, priority_id=261, animate_id=262, bind_id=263, copy_id=270, reset_id=280, game_base=300;
constexpr int reaction_id=281;
constexpr int game_tab_base=500,tool_tab_base=600,action_base=700;
int selected_game=0,tool_category=0;
nh::TicTacToe tic;
nh::Puzzle15 puzzle;
nh::LightsOut lights;
nh::SimonGame simon;
ULONGLONG simon_started=0;
int simon_flash=-1;
HWND window=nullptr;
std::map<int,HWND> controls;
std::map<int,bool> hovered;
std::map<int,RECT> desired_layout;
bool ui_test_mode=false;
bool active_window=false;
UINT timer_interval=0;
void schedule_timer();
void refresh_board();
struct PaintSurface {
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ original=nullptr;
    int width=0,height=0;
    void clear() {
        if(dc) { SelectObject(dc,original); DeleteObject(bitmap); DeleteDC(dc); }
        dc=nullptr; bitmap=nullptr; original=nullptr; width=height=0;
    }
    void resize(HDC reference,int w,int h) {
        if(dc&&width==w&&height==h) return;
        clear(); dc=CreateCompatibleDC(reference); bitmap=CreateCompatibleBitmap(reference,w,h);
        original=SelectObject(dc,bitmap); width=w; height=h;
    }
    ~PaintSurface() { clear(); }
} main_surface;
std::map<int,PaintSurface> button_surfaces;
std::array<HWND,3> key_fields{};
std::array<bool,3> bound{};
std::array<nh::Binding,3> pending_keys{};
std::array<nh::Observation,4> observations{};
nh::Settings settings;
nh::MemoryGame game;
std::vector<std::wstring> journal;
std::map<std::pair<int,int>,HFONT> fonts;
HANDLE scan_process=nullptr;
bool scanned=false;
int page=0;
double scale=1, nav_position=0, nav_from=0;
ULONGLONG started=0, nav_started=0, conceal_at=0;
ULONGLONG reaction_at=0;
bool reaction_ready=false;
std::wstring reaction_score=L"Начни тест, дождись сигнала и нажми кнопку.";
std::array<ULONGLONG,16> revealed_at{};
std::wstring defender=L"Не проверено", machine;

int px(int n) { return static_cast<int>(n*scale+0.5); }
int logical(int n) { return static_cast<int>(n/scale); }
Gdiplus::Color color(COLORREF c,BYTE a=255) { return {a,GetRValue(c),GetGValue(c),GetBValue(c)}; }
double monitor_scale(UINT dpi,HMONITOR monitor) {
    MONITORINFO m{}; m.cbSize=sizeof(m);
    if(!GetMonitorInfoW(monitor,&m)) return dpi/96.0;
    return std::min({dpi/96.0,(m.rcWork.right-m.rcWork.left)/1240.0,(m.rcWork.bottom-m.rcWork.top)/860.0});
}
void clear_fonts() { for(auto& f:fonts) DeleteObject(f.second); fonts.clear(); }
HFONT font(int size,int weight) {
    auto key=std::make_pair(size,weight);
    if(!fonts.count(key)) fonts[key]=CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    return fonts[key];
}
void text(HDC dc,const std::wstring& s,int x,int y,int w,int h,int size=14,COLORREF tint=ink,int weight=FW_NORMAL,bool wrap=false) {
    auto old=SelectObject(dc,font(size,weight)); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,tint);
    RECT r{px(x),px(y),px(x+w),px(y+h)};
    DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,DT_NOPREFIX|(wrap?DT_WORDBREAK:DT_SINGLELINE|DT_END_ELLIPSIS|DT_VCENTER));
    SelectObject(dc,old);
}
void shape(HDC dc,int x,int y,int w,int h,COLORREF fill,COLORREF outline=line,int radius=16) {
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    const float a=static_cast<float>(px(x))+0.5f,b=static_cast<float>(px(y))+0.5f;
    const float cw=static_cast<float>(px(w))-1,ch=static_cast<float>(px(h))-1,d=static_cast<float>(px(radius));
    path.AddArc(a,b,d,d,180,90); path.AddArc(a+cw-d,b,d,d,270,90);
    path.AddArc(a+cw-d,b+ch-d,d,d,0,90); path.AddArc(a,b+ch-d,d,d,90,90); path.CloseFigure();
    Gdiplus::SolidBrush brush(color(fill)); Gdiplus::Pen pen(color(outline),1);
    g.FillPath(&brush,&path); g.DrawPath(&pen,&path);
}
void log(const std::wstring& s) {
    SYSTEMTIME t{}; GetLocalTime(&t); wchar_t stamp[24]{};
    swprintf_s(stamp,L"%02u:%02u:%02u  ",t.wHour,t.wMinute,t.wSecond);
    journal.push_back(stamp+s); if(journal.size()>2000) journal.erase(journal.begin());
    if(window) InvalidateRect(window,nullptr,FALSE);
}
void result(const nh::Result& r) {
    log(r.message); MessageBoxW(window,r.message.c_str(),L"CrimeSQL dev system recovery",MB_OK|(r.success?MB_ICONINFORMATION:MB_ICONWARNING));
}
HWND button(int id,const wchar_t* label) {
    auto h=CreateWindowW(L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    controls[id]=h; return h;
}
LRESULT inspect_button_pixels(HWND control) {
    RECT client{}; GetClientRect(control,&client);
    const int width=client.right,height=client.bottom;
    if(width<=0||height<=0) return 0xC580;
    HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr; HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap||!pixels) { if(bitmap) DeleteObject(bitmap); DeleteDC(dc); return 0xC580; }
    auto original=SelectObject(dc,bitmap); auto data=static_cast<DWORD*>(pixels);
    const int count=width*height;
    auto whites=[&] { int n=0; for(int i=0;i<count;++i) if((data[i]&255)>220&&((data[i]>>8)&255)>220&&((data[i]>>16)&255)>220) ++n; return static_cast<double>(n)/count; };
    LRESULT flags=0;
    if(HDC displayed=GetDC(control)) {
        if(BitBlt(dc,0,0,width,height,displayed,0,0,SRCCOPY)) { GdiFlush(); if(whites()>0.75) flags|=8; }
        ReleaseDC(control,displayed);
    }
    std::fill_n(data,count,0x00010203);
    SendMessageW(control,WM_ERASEBKGND,reinterpret_cast<WPARAM>(dc),0); GdiFlush();
    if(whites()>=0.02) flags|=1;
    std::fill_n(data,count,0x00010203);
    SendMessageW(control,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT); GdiFlush();
    int untouched=0; for(int i=0;i<count;++i) if((data[i]&0xffffff)==0x010203) ++untouched;
    if(static_cast<double>(untouched)/count>=0.02) flags|=2;
    if(whites()>=0.2) flags|=4;
    SelectObject(dc,original); DeleteObject(bitmap); DeleteDC(dc);
    return 0xC500|flags;
}
LRESULT CALLBACK button_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR ref) {
    int id=static_cast<int>(ref);
    if(m==WM_APP+41&&ui_test_mode) return inspect_button_pixels(h);
    if(m==WM_ERASEBKGND) return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT) {
        PAINTSTRUCT ps{};
        HDC destination=m==WM_PAINT?BeginPaint(h,&ps):reinterpret_cast<HDC>(w);
        RECT client{}; GetClientRect(h,&client);
        if(destination&&client.right>0&&client.bottom>0) {
            auto& buffer=button_surfaces[id]; buffer.resize(destination,client.right,client.bottom);
            DRAWITEMSTRUCT item{}; item.CtlType=ODT_BUTTON; item.CtlID=static_cast<UINT>(id);
            item.itemAction=ODA_DRAWENTIRE; item.hwndItem=h; item.hDC=buffer.dc; item.rcItem=client;
            if(!IsWindowEnabled(h)) item.itemState|=ODS_DISABLED;
            if(GetFocus()==h) item.itemState|=ODS_FOCUS;
            if(SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED) item.itemState|=ODS_SELECTED;
            SendMessageW(GetParent(h),WM_DRAWITEM,id,reinterpret_cast<LPARAM>(&item));
            BitBlt(destination,0,0,client.right,client.bottom,buffer.dc,0,0,SRCCOPY);
        }
        if(m==WM_PAINT) EndPaint(h,&ps);
        return 0;
    }
    if(id>=400&&id<403&&(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)) {
        unsigned modifiers=((GetKeyState(VK_CONTROL)&0x8000)?MOD_CONTROL:0)|((GetKeyState(VK_MENU)&0x8000)?MOD_ALT:0)|((GetKeyState(VK_SHIFT)&0x8000)?MOD_SHIFT:0);
        nh::Binding candidate{modifiers,static_cast<unsigned>(w)};
        if(nh::valid_binding(candidate)) { pending_keys[static_cast<std::size_t>(id-400)]=candidate; SetWindowTextW(h,nh::binding_name(candidate).c_str()); }
        return 0;
    }
    if(m==WM_MOUSEMOVE&&!hovered[id]) { TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0}; TrackMouseEvent(&t); hovered[id]=true; InvalidateRect(h,nullptr,FALSE); }
    else if(m==WM_MOUSELEAVE) { hovered[id]=false; InvalidateRect(h,nullptr,FALSE); }
    else if(m==WM_NCDESTROY) { button_surfaces.erase(id); RemoveWindowSubclass(h,button_proc,1); }
    return DefSubclassProc(h,m,w,l);
}
void place(int id,int x,int y,int w,int h) {
    desired_layout[id]={px(x),px(y),px(x+w),px(y+h)};
}
std::wstring status(std::size_t i) {
    if(!scanned) return L"Ожидает диагностики";
    auto health=nh::classify(nh::policies[i],observations[i]);
    if(health==nh::Health::clear) return L"Ограничений не найдено";
    if(health==nh::Health::blocked) return L"Доступ ограничен политикой";
    return observations[i].state==nh::ReadState::denied?L"Нет доступа к настройке":L"Неизвестное значение или ошибка чтения";
}
void layout() {
    if(!window) return;
    RECT r{}; GetClientRect(window,&r); int width=logical(r.right),height=logical(r.bottom),x=270,c=width-x-32;
    if(r.right<=0||r.bottom<=0) { schedule_timer(); return; }
    desired_layout.clear();
    for(int i=0;i<7;++i) place(nav_base+i,18,181+i*48,200,42);
    if(page==0) {
        place(scan_id,x+26,289,190,42); place(mouse_id,x,526,(c-14)/2,54); place(keyboard_id,x+(c+14)/2,526,(c-14)/2,54);
        place(defender_id,x,596,c,45);
    } else if(page==1) {
        place(scan_id,x+c-190,52,190,42);
        for(int i=0;i<4;++i) {
            place(fix_base+i,x+c-166,196+i*88,145,38);
            EnableWindow(controls.at(fix_base+i),scanned&&nh::classify(nh::policies[i],observations[i])==nh::Health::blocked);
        }
        place(mouse_id,x,573,(c-14)/2,48); place(keyboard_id,x+(c+14)/2,573,(c-14)/2,48);
    } else if(page==2) place(lab_id,x+24,518,310,48);
    else if(page==3) {
        for(int i=0;i<nh::recovery_category_count;++i) place(tool_tab_base+i,x+i*((c+12)/nh::recovery_category_count),163,(c-48)/nh::recovery_category_count,36);
        for(int i=0;i<nh::recovery_category_size(tool_category);++i) place(action_base+tool_category*4+i,x+c-158,232+i*108,136,40);
    } else if(page==4) {
        place(autorun_id,x+c-170,181,145,38); place(priority_id,x+c-170,275,145,38); place(animate_id,x+c-170,369,145,38);
        SetWindowTextW(controls.at(autorun_id),nh::autorun_enabled()?L"Отключить":L"Включить");
        SetWindowTextW(controls.at(priority_id),settings.high_priority?L"Обычный":L"Высокий");
        SetWindowTextW(controls.at(animate_id),settings.animations?L"Выключить":L"Включить");
        for(int i=0;i<3;++i) place(400+i,x+c-275,483+i*51,250,34);
        place(bind_id,x+22,652,300,42);
    } else if(page==5) {
        for(int i=0;i<6;++i) place(game_tab_base+i,x+(i%3)*((c+12)/3),161+(i/3)*42,(c-24)/3,34);
        if(selected_game==0||selected_game==3) for(int i=0;i<16;++i) place(game_base+i,x+22+(i%4)*103,284+(i/4)*82,90,70);
        if(selected_game==2) for(int i=0;i<9;++i) place(game_base+i,x+32+(i%3)*129,287+(i/3)*101,113,86);
        if(selected_game==4) for(int i=0;i<4;++i) place(game_base+i,x+32+(i%2)*195,312+(i/2)*124,175,104);
        if(selected_game==5) for(int i=0;i<25;++i) place(game_base+i,x+26+(i%5)*78,283+(i/5)*64,65,53);
        place(reset_id,x+478,415,c-502,42);
        if(selected_game==1) place(reaction_id,x+35,294,c-70,96);
    } else place(copy_id,x,height-112,240,42);
    auto batch=BeginDeferWindowPos(static_cast<int>(controls.size()));
    for(const auto& entry:controls) {
        const bool visible=desired_layout.count(entry.first)!=0;
        const bool was_visible=(GetWindowLongPtrW(entry.second,GWL_STYLE)&WS_VISIBLE)!=0;
        RECT current{}; GetWindowRect(entry.second,&current); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&current),2);
        RECT target=visible?desired_layout.at(entry.first):current;
        if(visible==was_visible&&EqualRect(&target,&current)) continue;
        const UINT flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW);
        if(batch) batch=DeferWindowPos(batch,entry.second,nullptr,target.left,target.top,target.right-target.left,target.bottom-target.top,flags);
        else SetWindowPos(entry.second,nullptr,target.left,target.top,target.right-target.left,target.bottom-target.top,flags);
    }
    if(batch) EndDeferWindowPos(batch);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    schedule_timer();
}
void refresh_board() {
    for(int i=0;i<25;++i) InvalidateRect(controls.at(game_base+i),nullptr,FALSE);
    InvalidateRect(window,nullptr,FALSE); schedule_timer();
}
void diagnose() {
    for(std::size_t i=0;i<4;++i) observations[i]=nh::read_policy(nh::policies[i]);
    scanned=true; defender=nh::defender_status();
    for(std::size_t i=0;i<4;++i) log(std::wstring(nh::policies[i].name)+L": "+status(i)+(observations[i].error?L" / "+nh::error_text(observations[i].error):L""));
    log(L"Диагностика завершена. Наличие вирусов этой проверкой не определяется."); layout();
}
void set_key_fields() {
    for(std::size_t i=0;i<3;++i) {
        pending_keys[i]=settings.bindings[i];
        SetWindowTextW(key_fields[i],nh::binding_name(pending_keys[i]).c_str());
    }
}
bool register_keys(const std::array<nh::Binding,3>& keys) {
    for(int i=1;i<=3;++i) UnregisterHotKey(window,i);
    bool ok=true;
    for(std::size_t i=0;i<3;++i) {
        bound[i]=RegisterHotKey(window,static_cast<int>(i)+1,keys[i].modifiers|MOD_NOREPEAT,keys[i].key)!=FALSE;
        ok=ok&&bound[i];
    }
    return ok;
}
void apply_keys() {
    auto next=settings;
    next.bindings=pending_keys;
    for(std::size_t i=0;i<3;++i) {
        if(!nh::valid_binding(next.bindings[i])) { result({false,L"Используйте Ctrl или Alt с буквой, цифрой или F1–F24."}); return; }
        for(std::size_t j=0;j<i;++j) if(nh::same_binding(next.bindings[i],next.bindings[j])) { result({false,L"Назначьте разные сочетания для каждого действия."}); return; }
    }
    if(!register_keys(next.bindings)) { register_keys(settings.bindings); result({false,L"Сочетание занято другим приложением или Windows. Предыдущие назначения восстановлены там, где доступны."}); return; }
    auto saved=nh::save_settings(next);
    if(saved.success) settings=next; else register_keys(settings.bindings);
    result(saved); layout();
}
bool restore(std::size_t i) {
    if(!scanned||nh::classify(nh::policies[i],observations[i])!=nh::Health::blocked) return false;
    const auto& p=nh::policies[i];
    auto question=std::wstring(L"Восстановить доступ: ")+p.name+L"?\n\nБудет удалено значение HKCU\\"+p.key+L"\\"+p.value+L" (DWORD "+std::to_wstring(observations[i].value)+L").\nСначала сохраняется исходное значение в .reg.\n\nНа рабочем или учебном ПК согласуйте изменение с администратором.";
    if(MessageBoxW(window,question.c_str(),L"Восстановление с резервной копией",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES) return false;
    auto r=nh::restore_policy(p,observations[i]); result({r.success,r.message}); diagnose(); return r.success;
}
void task_manager() {
    diagnose();
    if(nh::classify(nh::policies[0],observations[0])==nh::Health::blocked&&!restore(0)) return;
    std::wstring error; if(!nh::open_tool(window,nh::Tool::task_manager,error)) result({false,error});
    else log(L"Windows передан запрос открытия диспетчера задач.");
}
void copy_log() {
    std::wstring s=L"CrimeSQL dev system recovery 0.3.1\r\n";
    for(auto& entry:journal) s+=entry+L"\r\n";
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t)); if(!memory) return;
    void* pointer=GlobalLock(memory); if(!pointer) { GlobalFree(memory); return; }
    std::memcpy(pointer,s.c_str(),(s.size()+1)*sizeof(wchar_t)); GlobalUnlock(memory);
    bool ok=false;
    if(OpenClipboard(window)) { if(EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory)) ok=true; CloseClipboard(); }
    if(!ok) GlobalFree(memory);
    result({ok,ok?L"Журнал скопирован. Проверьте пути к личным файлам перед отправкой.":L"Не удалось скопировать журнал."});
}
void command(int id) {
    if(id>=nav_base&&id<nav_base+7) {
        if(page==id-nav_base) return;
        nav_from=nav_position; nav_started=GetTickCount64(); page=id-nav_base;
        if(page==5&&selected_game==4&&simon.showing()) simon_started=GetTickCount64();
        layout(); return;
    }
    if(id>=game_tab_base&&id<game_tab_base+6) {
        selected_game=id-game_tab_base;
        if(selected_game==4&&simon.showing()) simon_started=GetTickCount64();
        layout(); refresh_board(); return;
    }
    if(id>=tool_tab_base&&id<tool_tab_base+nh::recovery_category_count) { tool_category=id-tool_tab_base; layout(); return; }
    if(id>=action_base&&id<action_base+nh::recovery_action_count) {
        const auto action=static_cast<nh::RecoveryAction>(id-action_base); const auto& info=nh::action_info(action);
        const bool maintenance=!nh::recovery_script(action,L"C:\\Windows\\System32").empty()||action==nh::RecoveryAction::restore_fonts;
        if(maintenance&&scan_process) { result({false,L"Другое окно операции ещё открыто. Закройте его после просмотра результата."}); return; }
        if(info.confirmation[0]&&MessageBoxW(window,info.confirmation,info.title,MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES) return;
        result(nh::launch_recovery_action(window,action,scan_process)); schedule_timer(); return;
    }
    if(id==scan_id) diagnose();
    else if(id>=fix_base&&id<fix_base+4) restore(static_cast<std::size_t>(id-fix_base));
    else if(id==mouse_id) result(nh::recover_mouse());
    else if(id==keyboard_id) {
        if(MessageBoxW(window,L"Отключить фильтрацию клавиш на текущий сеанс и запросить освобождение ввода? Чужие перехватчики это не удаляет.",L"Восстановление клавиатуры",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES) result(nh::recover_keyboard());
    } else if(id>=tools_base&&id<tools_base+5) {
        if(id==tools_base+1) task_manager();
        else { std::wstring error; if(!nh::open_tool(window,static_cast<nh::Tool>(id-tools_base),error)) result({false,error}); else log(L"Запрос открытия системного инструмента передан Windows."); }
    } else if(id==defender_id) {
        if(scan_process) { result({false,L"Другое окно операции ещё открыто. Закройте его после просмотра результата."}); return; }
        if(MessageBoxW(window,L"Запустить полную проверку Microsoft Defender? Windows запросит повышение прав. Проверка может занять длительное время.",L"Проверка на вирусы",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES) result(nh::start_defender_scan(window,scan_process));
    } else if(id==lab_id) {
        if(!nh::sandbox_available()) { result({false,L"Windows Sandbox недоступна. Нужны Windows Pro/Enterprise/Education, виртуализация и включённый компонент «Песочница Windows». На Home функция недоступна. EXE не запускался."}); return; }
        wchar_t path[32768]{}; OPENFILENAMEW d{}; d.lStructSize=sizeof(d); d.hwndOwner=window; d.lpstrFilter=L"Windows executable (*.exe)\0*.exe\0\0";
        d.lpstrFile=path; d.nMaxFile=32768; d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&d)) return;
        auto question=std::wstring(L"Запустить файл внутри Windows Sandbox?\n\n")+path+L"\n\nСеть, общий буфер обмена и запись в папки основного ПК отключены. В сеанс передаются только EXE и наблюдатель.\nЖурнал показывает изменения всей Sandbox, включая фоновые процессы.\n\nЭто не гарантия безопасности. Для образцов высокой опасности используйте отдельную тестовую машину. Продолжить?";
        if(MessageBoxW(window,question.c_str(),L"Изолированная лаборатория",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES) return;
        auto r=nh::launch_sandbox(window,path); result({r.success,r.message});
    } else if(id==autorun_id) {
        bool enable=!nh::autorun_enabled();
        if(!enable||MessageBoxW(window,(L"Добавить CrimeSQL в автозапуск текущего пользователя?\n\n"+nh::executable_path()+L"\n\nПриложение будет запускаться свёрнутым. Не перемещайте его папку.").c_str(),L"Автозапуск",MB_YESNO|MB_DEFBUTTON2)==IDYES) result(nh::set_autorun(enable));
        layout();
    } else if(id==priority_id||id==animate_id) {
        auto next=settings;
        if(id==priority_id) { next.high_priority=!next.high_priority; auto r=nh::set_high_priority(next.high_priority); if(!r.success) { result(r); return; } }
        else next.animations=!next.animations;
        auto saved=nh::save_settings(next);
        if(saved.success) settings=next; else if(id==priority_id) nh::set_high_priority(settings.high_priority);
        result(saved); layout();
    } else if(id==bind_id) apply_keys();
    else if(id==copy_id) copy_log();
    else if(id==reset_id) {
        const auto seed=static_cast<unsigned>(GetTickCount64());
        if(selected_game==0) { game.reset(seed); conceal_at=0; revealed_at.fill(0); }
        if(selected_game==1) { reaction_at=0; reaction_ready=false; reaction_score=L"Дождись сигнала и нажми кнопку."; SetWindowTextW(controls.at(reaction_id),L"Начать тест реакции"); }
        if(selected_game==2) tic.reset();
        if(selected_game==3) puzzle.reset(seed);
        if(selected_game==4) { simon.reset(seed); simon_started=GetTickCount64(); simon_flash=-1; }
        if(selected_game==5) lights.reset(seed);
        refresh_board();
    }
    else if(id==reaction_id) {
        auto now=GetTickCount64();
        if(!reaction_at) { reaction_at=now+1500+(now%2500); reaction_ready=false; reaction_score=L"Жди сигнала… Не нажимай раньше времени."; SetWindowTextW(controls.at(reaction_id),L"Жди сигнала…"); }
        else {
            reaction_score=now<reaction_at?L"Слишком рано! Попробуй ещё раз.":L"Твоя реакция: "+std::to_wstring(now-reaction_at)+L" мс";
            reaction_at=0; reaction_ready=false; SetWindowTextW(controls.at(reaction_id),L"Начать тест реакции");
        }
        InvalidateRect(window,nullptr,FALSE);
    }
    else if(id>=game_base&&id<game_base+25) {
        const int index=id-game_base;
        if(selected_game==0&&game.choose(index)) {
            revealed_at[static_cast<std::size_t>(index)]=GetTickCount64();
            if(game.waiting()) conceal_at=GetTickCount64()+850;
            if(game.found()==8) log(L"Все пары найдены! Ходов: "+std::to_wstring(game.moves()));
        }
        if(selected_game==2) tic.play(index);
        if(selected_game==3) puzzle.slide(index);
        if(selected_game==4) {
            if(simon.accept(index)==nh::SimonGame::Answer::round_complete) { simon_started=GetTickCount64()+650; simon_flash=-1; }
        }
        if(selected_game==5) lights.toggle(index);
        refresh_board();
    }
}
void orbit(HDC dc,int x,int y,double phase) {
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float cx=static_cast<float>(px(x)),cy=static_cast<float>(px(y));
    for(int i=0;i<3;++i) { float r=static_cast<float>(px(45+i*22)); Gdiplus::Pen p(color(violet,static_cast<BYTE>(70-i*14)),1); g.DrawEllipse(&p,cx-r,cy-r,r*2,r*2); }
    Gdiplus::SolidBrush light(color(teal)); float r=static_cast<float>(px(89)),dx=cx+static_cast<float>(std::cos(phase))*r,dy=cy+static_cast<float>(std::sin(phase))*r;
    g.FillEllipse(&light,dx-px(4),dy-px(4),static_cast<float>(px(8)),static_cast<float>(px(8)));
    Gdiplus::PointF points[]{{cx,cy-px(37)},{cx+px(30),cy-px(24)},{cx+px(26),cy+px(16)},{cx,cy+px(39)},{cx-px(26),cy+px(16)},{cx-px(30),cy-px(24)}};
    Gdiplus::SolidBrush body(color(RGB(30,58,63))); Gdiplus::Pen edge(color(teal),static_cast<float>(px(2)));
    g.FillPolygon(&body,points,6); g.DrawPolygon(&edge,points,6);
    Gdiplus::Pen tick(color(teal),static_cast<float>(px(3)));
    g.DrawLine(&tick,cx-px(12),cy,cx-px(3),cy+px(9)); g.DrawLine(&tick,cx-px(3),cy+px(9),cx+px(15),cy-px(11));
}
void paint(HDC dc) {
    RECT r{}; GetClientRect(window,&r); int width=logical(r.right),height=logical(r.bottom),x=270,c=width-x-32;
    auto brush=CreateSolidBrush(bg); FillRect(dc,&r,brush); DeleteObject(brush);
    RECT sr{0,0,px(236),r.bottom}; brush=CreateSolidBrush(side); FillRect(dc,&sr,brush); DeleteObject(brush);
    shape(dc,22,28,48,48,RGB(38,33,66),RGB(60,48,96)); text(dc,L"C/",32,32,42,39,25,violet,FW_BOLD);
    text(dc,L"CrimeSQL",82,27,145,31,23,ink,FW_BOLD); text(dc,L"DEV / SYSTEM RECOVERY",83,60,140,18,9,muted,FW_SEMIBOLD);
    shape(dc,22,108,192,31,RGB(20,39,38),RGB(35,65,58),18); text(dc,L"●  LOCAL-FIRST / v0.3.1",36,110,173,26,10,teal,FW_SEMIBOLD);
    text(dc,L"РАБОЧЕЕ ПРОСТРАНСТВО",24,150,190,20,10,muted,FW_SEMIBOLD);
    shape(dc,18,181+static_cast<int>(nav_position*48),200,42,RGB(42,35,67),RGB(66,53,102),14);
    shape(dc,18,height-172,200,100,panel); text(dc,L"АВАРИЙНЫЙ ВХОД",32,height-160,170,19,10,muted,FW_BOLD);
    text(dc,L"Ctrl + Alt + Del",32,height-135,170,25,16,teal,FW_SEMIBOLD);
    text(dc,L"Если ввод полностью заблокирован",32,height-104,170,30,10,muted,FW_NORMAL,true);
    text(dc,L"CrimeSQL dev • Windows 10 / 11",24,height-43,196,24,10,muted);
    const wchar_t* titles[]{L"Верни контроль.",L"Восстановление доступа",L"Лаборатория EXE",L"Системные инструменты",L"Твой центр управления",L"Игровая комната",L"Журнал действий"};
    const wchar_t* captions[]{L"Диагностика, восстановление и изолированный анализ — в одном месте.",L"Распознанные ограничения профиля. Изменения — с резервной копией.",L"Наблюдай за изменениями файлов внутри Windows Sandbox.",L"Средства Windows для проверки и восстановления системы.",L"Горячие клавиши, автозапуск и поведение приложения.",L"Шесть мини-игр без сетевых сервисов. Небольшой перерыв.",L"История сеанса: до 2000 записей, без отправки на сервер."};
    text(dc,L"CRIMESQL DEV SYSTEM RECOVERY",x,24,c,20,10,violet,FW_BOLD);
    text(dc,titles[page],x,57,c-(page==1?200:0),44,31,ink,FW_BOLD); text(dc,captions[page],x,111,c,30,13,muted);
    if(page==0) {
        shape(dc,x,163,c,192,RGB(23,27,45),RGB(51,48,77),24);
        text(dc,L"ТВОЯ СИСТЕМА. ТВОИ ПРАВИЛА.",x+26,181,c-250,24,10,teal,FW_BOLD);
        text(dc,L"Начни с диагностики",x+26,214,c-250,36,25,ink,FW_BOLD);
        text(dc,L"Четыре проверки. Без автоматических изменений.",x+26,258,c-255,24,12,muted);
        orbit(dc,x+c-126,259,settings.animations?(GetTickCount64()-started)/2200.0:0.6);
        int card=(c-28)/3,blocked=0,unknown=0;
        for(std::size_t i=0;i<4&&scanned;++i) { blocked+=nh::classify(nh::policies[i],observations[i])==nh::Health::blocked; unknown+=nh::classify(nh::policies[i],observations[i])==nh::Health::unknown; }
        for(int i=0;i<3;++i) shape(dc,x+i*(card+14),373,card,125,panel);
        text(dc,L"ОГРАНИЧЕНИЯ ПРОФИЛЯ",x+18,385,card-30,23,10,muted,FW_BOLD);
        text(dc,scanned?std::to_wstring(blocked):L"—",x+18,412,48,42,33,blocked?amber:teal,FW_BOLD);
        text(dc,scanned?L"из 4 проверок":L"не проверено",x+68,427,card-83,20,12,muted);
        text(dc,unknown?L"Есть непрочитанные настройки":L"Только текущий пользователь",x+18,466,card-30,19,10,muted);
        text(dc,L"MICROSOFT DEFENDER",x+card+32,385,card-30,23,10,muted,FW_BOLD); text(dc,defender,x+card+32,419,card-30,28,16,ink,FW_SEMIBOLD);
        text(dc,L"Статус службы, не результат проверки",x+card+32,466,card-30,19,10,muted);
        text(dc,L"ТВОЙ КОМПЬЮТЕР",x+2*(card+14)+18,385,card-30,23,10,muted,FW_BOLD);
        text(dc,machine,x+2*(card+14)+18,419,card-30,28,16,ink,FW_SEMIBOLD); text(dc,L"Без облачной отправки данных",x+2*(card+14)+18,466,card-30,19,10,muted);
        text(dc,L"БЫСТРОЕ ВОССТАНОВЛЕНИЕ",x,500,c,22,10,muted,FW_BOLD);
        text(dc,L"Восстановление настроек не удаляет вирусы. Для поиска угроз запусти проверку Defender.",x,661,c,42,12,muted,FW_NORMAL,true);
    } else if(page==1) {
        for(std::size_t i=0;i<4;++i) {
            int y=175+static_cast<int>(i)*88; shape(dc,x,y,c,76,panel);
            shape(dc,x+16,y+17,39,39,RGB(36,34,57),RGB(36,34,57),12); text(dc,L"0"+std::to_wstring(i+1),x+24,y+20,32,31,14,violet,FW_BOLD);
            text(dc,nh::policies[i].name,x+72,y+10,c-260,28,17,ink,FW_SEMIBOLD);
            text(dc,status(i),x+72,y+41,c-260,23,12,!scanned?muted:nh::classify(nh::policies[i],observations[i])==nh::Health::clear?teal:amber);
        }
        text(dc,L"ВОССТАНОВЛЕНИЕ ВВОДА",x,542,c,24,10,muted,FW_BOLD);
        text(dc,L"Чужие блокировки и драйверы могут остаться. При полном отказе: Ctrl+Alt+Del.",x,641,c,44,12,amber,FW_NORMAL,true);
        text(dc,L"На рабочем или учебном ПК согласуй изменение политик с администратором.",x,696,c,28,11,muted);
    } else if(page==2) {
        shape(dc,x,164,c,425,panel,RGB(53,50,83),24);
        text(dc,L"EXE LAB / ИЗОЛИРОВАННЫЙ СЕАНС",x+24,183,c-48,26,11,violet,FW_BOLD);
        text(dc,L"Посмотри, что меняется",x+24,223,c-48,40,28,ink,FW_BOLD);
        text(dc,L"Выбери EXE. Приложение подготовит отдельную Sandbox, запустит наблюдатель и только затем — образец внутри неё.",x+24,277,c-65,62,15,muted,FW_NORMAL,true);
        const wchar_t* items[]{L"+  Созданные и изменённые файлы",L"−  Удалённые файлы и папки",L"↗  Переименования: старый → новый путь"};
        for(int i=0;i<3;++i) text(dc,items[i],x+26,355+i*36,c-60,29,15,i==2?violet:teal,FW_SEMIBOLD);
        text(dc,L"Сеть: выкл.   Буфер обмена: выкл.   Папка образца: только чтение.",x+24,473,c-48,25,12,muted);
        text(dc,nh::sandbox_available()?L"Windows Sandbox найдена":L"Sandbox не найдена — EXE не запустится",x+350,525,c-380,35,12,nh::sandbox_available()?teal:amber,FW_SEMIBOLD,true);
        text(dc,L"Журнал — внутри Sandbox; при закрытии он удаляется. Это события всей среды, включая фоновые процессы. Они не доказывают, что конкретный EXE создал каждый файл.",x,610,c,68,13,muted,FW_NORMAL,true);
        text(dc,L"Нужны Windows Pro / Enterprise / Education, виртуализация и компонент Sandbox. На Home лаборатория недоступна.",x,696,c,48,12,amber,FW_NORMAL,true);
    } else if(page==3) {
        for(int i=0;i<nh::recovery_category_size(tool_category);++i) {
            const auto& info=nh::action_info(static_cast<nh::RecoveryAction>(tool_category*4+i));
            shape(dc,x,216+i*108,c,92,panel);
            text(dc,info.title,x+20,228+i*108,c-210,29,17,ink,FW_SEMIBOLD);
            text(dc,info.description,x+20,265+i*108,c-210,31,12,muted,FW_NORMAL,true);
        }
        if(tool_category==4) {
            text(dc,L"КАК ЭТО РАБОТАЕТ",x+20,466,c-40,26,12,teal,FW_BOLD);
            text(dc,L"Сначала — копия .reg в Backups. Затем сброс подмен Segoe UI и восстановление шрифтов интерфейса. Для применения выйди из учётной записи и войди снова.",x+20,505,c-40,66,14,muted,FW_NORMAL,true);
            text(dc,L"Если файлы шрифтов удалены или повреждены: Инструменты → Система → проверка SFC, затем SFC/DISM. Файлы из сети не скачиваются; прочие установленные шрифты сохраняются.",x+20,592,c-40,66,13,amber,FW_NORMAL,true);
        }
        text(dc,scan_process?L"Окно операции открыто. Результат и путь к копии или отчёту показаны в нём.":
            tool_category==0?L"Проверки выполняет Microsoft Defender. Автономная проверка перезагружает компьютер после подтверждения.":
            tool_category==1?L"SFC/DISM исправляют повреждения Windows. Обнаружением и удалением угроз занимается антивирус.":
            tool_category==2?L"PowerShell открывается в текущей Windows. В WinRE штатно есть CMD; PowerShell обычно не установлен.":
            tool_category==4?L"Без автоматической перезагрузки или выхода. Восстановление настроек не проверяет целостность файлов шрифтов.":
            L"Отчёты могут содержать личные пути и команды автозапуска. Проверь их перед отправкой.",x,679,c,53,12,muted,FW_NORMAL,true);
    } else if(page==4) {
        const wchar_t* names[]{L"Запускать вместе с Windows",L"Приоритет приложения",L"Плавные анимации"};
        std::wstring notes[]{nh::autorun_enabled()?L"Включён • для текущего пользователя":L"Выключен • запуск свёрнутым после входа",settings.high_priority?L"Высокий • только процесс CrimeSQL":L"Обычный • рекомендуемый режим",settings.animations?L"Включены • движение акцентов":L"Выключены • уменьшенное движение"};
        for(int i=0;i<3;++i) { shape(dc,x,164+i*94,c,80,panel); text(dc,names[i],x+20,176+i*94,c-220,27,17,ink,FW_SEMIBOLD); text(dc,notes[i],x+20,209+i*94,c-220,22,12,muted); }
        shape(dc,x,459,c,250,panel);
        const wchar_t* labels[]{L"Освободить мышь",L"Восстановить клавиатуру",L"Восстановить / открыть диспетчер"};
        for(int i=0;i<3;++i) { text(dc,labels[i],x+22,482+i*51,c-320,24,14,ink,FW_SEMIBOLD); text(dc,bound[i]?L"Бинд зарегистрирован":L"Бинд недоступен — выбери другой",x+22,506+i*51,c-320,17,10,bound[i]?teal:amber); }
        text(dc,L"Нажми сочетание в поле: Ctrl/Alt + буква, цифра или F1–F24. Бинды работают, пока приложение запущено.",x,725,c,38,11,muted,FW_NORMAL,true);
    } else if(page==5) {
        if(selected_game!=1) shape(dc,x,267,440,350,panel);
        const wchar_t* names[]{L"НАЙДИ ПАРЫ",L"ТЕСТ РЕАКЦИИ",L"КРЕСТИКИ-НОЛИКИ",L"ПЯТНАШКИ",L"САЙМОН",L"LIGHTS OUT"};
        text(dc,names[selected_game],selected_game==1?x+22:x+478,selected_game==1?252:276,selected_game==1?c-44:c-500,26,12,teal,FW_BOLD);
        std::wstring score,help;
        if(selected_game==0) { score=std::to_wstring(game.found())+L" / 8"; help=L"Ходов: "+std::to_wstring(game.moves())+L". Открывай по две карточки и запоминай совпадения."; }
        if(selected_game==1) { score=L"Реакция"; help=reaction_score; }
        if(selected_game==2) { score=tic.status()==2?L"Победа O":tic.status()==3?L"Ничья":tic.status()==1?L"Победа X":L"Твой ход"; help=L"Ты играешь крестиками. Собери три в ряд. Компьютер отвечает ноликами."; }
        if(selected_game==3) { score=puzzle.solved()?L"Собрано!":L"Ходы: "+std::to_wstring(puzzle.moves()); help=L"Расположи плитки 1–15 по порядку. Нажимай плитку рядом с пустой клеткой. Каждая партия решаема."; }
        if(selected_game==4) { score=simon.lost()?L"Ошибка":L"Уровень "+std::to_wstring(simon.sequence().size()); help=simon.lost()?L"Последовательность нарушена. Начни новую игру.":simon.showing()?L"Смотри на подсветку и запоминай последовательность.":L"Повтори показанную последовательность кнопками 1–4."; }
        if(selected_game==5) { score=lights.solved()?L"Победа!":L"Ходы: "+std::to_wstring(lights.moves()); help=L"Погаси все огни. Нажатие переключает клетку и четырёх соседей. Партии имеют решение."; }
        if(selected_game!=1) text(dc,score,x+478,327,c-500,56,30,ink,FW_BOLD);
        text(dc,help,x+478,482,c-500,118,14,muted,FW_NORMAL,true);
        text(dc,L"Мини-игры не изменяют настройки системы и не влияют на защиту.",x,686,c,36,12,muted);
    } else {
        shape(dc,x,165,c,height-306,panel);
        auto capacity=static_cast<std::size_t>(std::max(1,(height-344)/49)); auto first=journal.size()>capacity?journal.size()-capacity:0; int y=180;
        for(std::size_t i=first;i<journal.size();++i,y+=49) text(dc,journal[i],x+20,y,c-40,45,12,muted,FW_NORMAL,true);
        text(dc,L"Кнопка копирует весь журнал. Пути могут содержать имя пользователя.",x+260,height-110,c-260,44,11,muted,FW_NORMAL,true);
    }
    text(dc,L"CrimeSQL dev system recovery / LOCAL TOOLS. CLEAR ACTIONS.",x,height-33,c,19,10,muted);
}
void schedule_timer() {
    if(!window) return;
    const auto now=GetTickCount64();
    UINT interval=0;
    const bool moving=settings.animations&&now<nav_started+220;
    if(moving||(settings.animations&&page==0&&active_window&&!IsIconic(window))) interval=33;
    if(settings.animations&&page==5&&selected_game==0) for(auto timestamp:revealed_at)
        if(timestamp&&now-timestamp<190) interval=33;
    if(conceal_at||(reaction_at&&!reaction_ready&&page==5&&selected_game==1)) interval=interval?interval:50;
    if(page==5&&selected_game==4&&simon.showing()&&active_window&&!IsIconic(window)) interval=interval?interval:50;
    if(scan_process&&!interval) interval=1000;
    if(interval==timer_interval) return;
    KillTimer(window,1); timer_interval=interval;
    if(interval) SetTimer(window,1,interval,nullptr);
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_APP+42&&ui_test_mode) return timer_interval;
    switch(message) {
        case WM_CREATE: {
            window=hwnd; scale=monitor_scale(GetDpiForWindow(hwnd),MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST));
            settings=ui_test_mode?nh::Settings{}:nh::load_settings(); started=GetTickCount64();
            const wchar_t* nav[]{L"01   Панель управления",L"02   Восстановление",L"03   Лаборатория EXE",L"04   Инструменты",L"05   Настройки и бинды",L"06   Перерыв",L"07   Журнал действий"};
            for(int i=0;i<7;++i) button(nav_base+i,nav[i]);
            button(scan_id,L"Проверить систему  →"); for(int i=0;i<4;++i) button(fix_base+i,L"Восстановить");
            button(mouse_id,L"Освободить мышь"); button(keyboard_id,L"Восстановить клавиатуру");
            for(int i=0;i<5;++i) button(tools_base+i,L"Открыть  ↗");
            button(defender_id,L"Запустить полную проверку Microsoft Defender"); button(lab_id,L"Выбрать EXE и открыть Sandbox  ↗");
            button(autorun_id,L"Включить"); button(priority_id,L"Высокий"); button(animate_id,L"Выключить"); button(bind_id,L"Сохранить и применить бинды");
            button(copy_id,L"Копировать весь журнал"); button(reset_id,L"Новая игра"); for(int i=0;i<25;++i) button(game_base+i,L"Клетка");
            button(reaction_id,L"Начать тест реакции");
            const wchar_t* games[]{L"Пары",L"Реакция",L"Крестики-нолики",L"Пятнашки",L"Саймон",L"Lights Out"};
            for(int i=0;i<6;++i) button(game_tab_base+i,games[i]);
            const wchar_t* categories[]{L"Защита",L"Система",L"Консоли / WinRE",L"Отчёты",L"Шрифты"};
            for(int i=0;i<nh::recovery_category_count;++i) button(tool_tab_base+i,categories[i]);
            for(int i=0;i<nh::recovery_action_count;++i) button(action_base+i,i==static_cast<int>(nh::RecoveryAction::restore_fonts)?L"Восстановить":nh::recovery_script(static_cast<nh::RecoveryAction>(i),L"C:\\Windows\\System32").empty()?L"Открыть  →":L"Запустить  →");
            for(auto& entry:controls) SetWindowSubclass(entry.second,button_proc,1,static_cast<DWORD_PTR>(entry.first));
            for(int i=0;i<3;++i) { key_fields[static_cast<std::size_t>(i)]=button(400+i,L"Нажмите сочетание"); SetWindowSubclass(key_fields[static_cast<std::size_t>(i)],button_proc,1,400+i); }
            set_key_fields(); if(!register_keys(settings.bindings)) log(L"Некоторые горячие клавиши заняты. Измените их в Настройках.");
            if(settings.high_priority) { auto r=nh::set_high_priority(true); if(!r.success) { settings.high_priority=false; log(r.message); } }
            BOOL animation=TRUE; if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0)&&!animation) settings.animations=false;
            machine=nh::system_summary(); game.reset(static_cast<unsigned>(GetTickCount64()));
            puzzle.reset(static_cast<unsigned>(started)); lights.reset(static_cast<unsigned>(started)); simon.reset(static_cast<unsigned>(started));
            log(L"CrimeSQL 0.3.1 запущен. Восстановление без команды пользователя не выполняется.");
            layout(); return 0;
        }
        case WM_TIMER: {
            auto now=GetTickCount64(); double progress=settings.animations?std::min(1.0,(now-nav_started)/220.0):1.0;
            double previous=nav_position; nav_position=nav_from+(page-nav_from)*(1-std::pow(1-progress,3));
            if(previous!=nav_position) { RECT dirty{px(16),px(179),px(220),px(181+7*48)}; InvalidateRect(hwnd,&dirty,FALSE); }
            if(settings.animations&&page==0&&active_window&&!IsIconic(hwnd)) {
                RECT client{}; GetClientRect(hwnd,&client);
                RECT dirty{client.right-px(253),px(167),client.right-px(62),px(352)};
                InvalidateRect(hwnd,&dirty,FALSE);
            }
            if(conceal_at&&now>=conceal_at) { game.conceal(); conceal_at=0; for(int i=0;i<16;++i) InvalidateRect(controls.at(game_base+i),nullptr,FALSE); }
            if(reaction_at&&!reaction_ready&&now>=reaction_at) { reaction_ready=true; SetWindowTextW(controls.at(reaction_id),L"СЕЙЧАС! Нажми"); }
            if(settings.animations&&page==5&&selected_game==0) for(int i=0;i<16;++i) if(revealed_at[static_cast<std::size_t>(i)]&&now-revealed_at[static_cast<std::size_t>(i)]<190) InvalidateRect(controls.at(game_base+i),nullptr,FALSE);
            if(page==5&&selected_game==4&&simon.showing()&&active_window&&!IsIconic(hwnd)) {
                int flash=-1;
                if(now>=simon_started) {
                    const auto elapsed=now-simon_started, index=elapsed/600;
                    if(index>=simon.sequence().size()) { simon.allow_input(); InvalidateRect(hwnd,nullptr,FALSE); }
                    else if(elapsed%600<350) flash=simon.sequence()[static_cast<std::size_t>(index)];
                }
                if(flash!=simon_flash) { simon_flash=flash; for(int i=0;i<4;++i) InvalidateRect(controls.at(game_base+i),nullptr,FALSE); }
            }
            if(scan_process) {
                DWORD code=STILL_ACTIVE;
                if(GetExitCodeProcess(scan_process,&code)&&code!=STILL_ACTIVE) {
                    CloseHandle(scan_process); scan_process=nullptr;
                    log(L"Процесс окна операции завершился с кодом "+std::to_wstring(code)+L". Это не оценка отсутствия угроз. Результат и путь к копии/отчёту показаны в окне операции.");
                }
            }
            schedule_timer(); return 0;
        }
        case WM_HOTKEY: {
            int focused=GetDlgCtrlID(GetFocus());
            if(focused>=400&&focused<403&&w>=1&&w<=3) {
                pending_keys[static_cast<std::size_t>(focused-400)]=settings.bindings[static_cast<std::size_t>(w-1)];
                SetWindowTextW(GetFocus(),nh::binding_name(pending_keys[static_cast<std::size_t>(focused-400)]).c_str());
            } else if(w==1) command(mouse_id); else if(w==2) command(keyboard_id); else if(w==3) task_manager();
            schedule_timer(); return 0;
        }
        case WM_SIZE: if(w!=SIZE_MINIMIZED) layout(); else schedule_timer(); return 0;
        case WM_ACTIVATE:
            active_window=LOWORD(w)!=WA_INACTIVE;
            if(active_window&&page==5&&selected_game==4&&simon.showing()) simon_started=GetTickCount64();
            schedule_timer(); return 0;
        case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={px(1200),px(850)}; return 0;
        case WM_DPICHANGED: {
            scale=monitor_scale(HIWORD(w),MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST)); clear_fonts();
            auto rect=reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,rect->left,rect->top,px(1240),px(860),SWP_NOZORDER|SWP_NOACTIVATE); layout(); return 0;
        }
        case WM_COMMAND: if(HIWORD(w)==BN_CLICKED) command(LOWORD(w)); schedule_timer(); return 0;
        case WM_DRAWITEM: {
            auto original=reinterpret_cast<DRAWITEMSTRUCT*>(l);
            auto copy=*original; auto item=&copy;
            int id=static_cast<int>(item->CtlID),width=logical(item->rcItem.right),height=logical(item->rcItem.bottom);
            auto& surface=button_surfaces[id]; const bool direct=item->hDC!=surface.dc;
            if(direct) { surface.resize(item->hDC,item->rcItem.right,item->rcItem.bottom); item->hDC=surface.dc; }
            bool nav=id>=nav_base&&id<nav_base+7;
            bool active=(nav&&id-nav_base==page)||(id>=game_tab_base&&id<game_tab_base+6&&id-game_tab_base==selected_game)||
                (id>=tool_tab_base&&id<tool_tab_base+nh::recovery_category_count&&id-tool_tab_base==tool_category);
            bool primary=id==scan_id||id==lab_id||id==bind_id;
            bool card=id>=game_base&&id<game_base+25;
            bool binding=id>=400&&id<403;
            bool inside_panel=card||(page==1&&id>=fix_base&&id<fix_base+4)||(page==2&&id==lab_id)||(page==3&&id>=action_base&&id<action_base+nh::recovery_action_count)||page==4;
            COLORREF behind=nav?side:(page==0&&id==scan_id)?RGB(23,27,45):inside_panel?panel:bg;
            auto brush=CreateSolidBrush(behind); FillRect(item->hDC,&item->rcItem,brush); DeleteObject(brush);
            COLORREF fill=primary?teal:active?RGB(42,35,67):hovered[id]?RGB(36,43,61):binding?RGB(27,34,51):nav?side:panel;
            if(item->itemState&ODS_SELECTED) fill=RGB(49,59,80);
            shape(item->hDC,0,0,width,height,fill,active?RGB(74,59,116):binding?line:fill,14);
            wchar_t label[256]{}; GetWindowTextW(item->hwndItem,label,256); std::wstring title=label; int size=13,margin=15;
            if(card&&selected_game==0&&id<game_base+16) {
                int index=id-game_base; bool visible=game.visible(index);
                title=visible?std::wstring(1,static_cast<wchar_t>(L'A'+game.value(index))):L"";
                const auto age=GetTickCount64()-revealed_at[static_cast<std::size_t>(index)];
                const double progress=settings.animations&&visible?std::min(1.0,age/180.0):1.0;
                const int card_width=static_cast<int>(width*(0.25+0.75*std::sin(progress*1.57079632679)));
                shape(item->hDC,(width-card_width)/2,0,card_width,height,visible?RGB(47,40,75):RGB(27,34,51),visible?violet:line,20);
                if(!visible) {
                    Gdiplus::Graphics graphics(item->hDC); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                    const float cx=static_cast<float>(px(width/2)),cy=static_cast<float>(px(height/2)),radius=static_cast<float>(px(12));
                    Gdiplus::PointF diamond[]{{cx,cy-radius},{cx+radius,cy},{cx,cy+radius},{cx-radius,cy}};
                    Gdiplus::Pen edge(color(violet),1.5f); graphics.DrawPolygon(&edge,diamond,4);
                    Gdiplus::SolidBrush centre(color(teal)); graphics.FillEllipse(&centre,cx-px(2),cy-px(2),static_cast<float>(px(4)),static_cast<float>(px(4)));
                }
                if(progress<0.45) title.clear();
                size=30; margin=35;
            } else if(card) {
                const int index=id-game_base;
                COLORREF card_fill=RGB(27,34,51),edge=line;
                title.clear(); size=28;
                if(selected_game==2&&index<9) { const int value=tic.value(index); title=value==1?L"X":value==2?L"O":L""; edge=value?violet:line; }
                if(selected_game==3&&index<16) { const int value=puzzle.value(index); title=value?std::to_wstring(value):L""; if(!value) card_fill=panel; }
                if(selected_game==4&&index<4) {
                    constexpr COLORREF colors[]{RGB(58,173,145),RGB(147,110,220),RGB(211,157,78),RGB(78,146,214)};
                    const auto tint=colors[index];
                    const bool flash=simon_flash==index;
                    card_fill=flash?tint:RGB(GetRValue(tint)/3,GetGValue(tint)/3,GetBValue(tint)/3);
                    edge=flash?ink:tint; title=std::to_wstring(index+1);
                }
                if(selected_game==5&&index<25) { const bool lit=lights.value(index); card_fill=lit?RGB(44,123,103):RGB(23,31,44); edge=lit?teal:line; title=lit?L"+":L""; }
                if(hovered[id]&&selected_game!=4) edge=violet;
                shape(item->hDC,0,0,width,height,card_fill,edge,16);
                margin=std::max(8,width/2-(title.size()>1?18:10));
            }
            text(item->hDC,title,margin,0,width-margin-6,height,size,(item->itemState&ODS_DISABLED)?RGB(76,91,112):primary?bg:(active||binding)?violet:ink,FW_SEMIBOLD);
            if(item->itemState&ODS_FOCUS) { auto area=item->rcItem; InflateRect(&area,-px(4),-px(4)); DrawFocusRect(item->hDC,&area); }
            if(direct) BitBlt(original->hDC,0,0,item->rcItem.right,item->rcItem.bottom,surface.dc,0,0,SRCCOPY);
            return TRUE;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); RECT r{}; GetClientRect(hwnd,&r);
            if(r.right>0&&r.bottom>0) {
                main_surface.resize(dc,r.right,r.bottom);
                HDC buffer=main_surface.dc; const int saved=SaveDC(buffer);
                IntersectClipRect(buffer,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right,ps.rcPaint.bottom);
                const RECT orbit_area{r.right-px(253),px(167),r.right-px(62),px(352)};
                RECT intersection{}; IntersectRect(&intersection,&orbit_area,&ps.rcPaint);
                if(page==0&&EqualRect(&intersection,&ps.rcPaint)) {
                    auto brush=CreateSolidBrush(RGB(23,27,45)); FillRect(buffer,&orbit_area,brush); DeleteObject(brush);
                    orbit(buffer,logical(r.right)-158,259,settings.animations?(GetTickCount64()-started)/2200.0:0.6);
                } else paint(buffer);
                RestoreDC(buffer,saved);
                BitBlt(dc,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,
                    buffer,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);
            }
            EndPaint(hwnd,&ps); return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd,1); main_surface.clear();
            for(int i=1;i<=3;++i) UnregisterHotKey(hwnd,i);
            if(scan_process) CloseHandle(scan_process);
            clear_fonts(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int show) {
    const std::wstring command_line=arguments;
    if(command_line.rfind(L"--restore-fonts ",0)==0) {
        const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        const auto restored=nh::restore_system_fonts(command_line.substr(16));
        MessageBoxW(nullptr,restored.message.c_str(),L"CrimeSQL — восстановление шрифтов",MB_OK|(restored.success?MB_ICONINFORMATION:MB_ICONWARNING));
        if(SUCCEEDED(com)) CoUninitialize();
        return restored.success?0:1;
    }
    ui_test_mode=std::wstring(arguments)==L"--ui-test";
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    HANDLE single_instance=CreateMutexW(nullptr,FALSE,L"Local\\CrimeSQLRecovery.SingleInstance");
    if(single_instance&&GetLastError()==ERROR_ALREADY_EXISTS) {
        if(auto existing=FindWindowW(L"CrimeSQLRecovery",nullptr)) { ShowWindow(existing,SW_RESTORE); SetForegroundWindow(existing); }
        CloseHandle(single_instance); if(SUCCEEDED(com)) CoUninitialize(); return 0;
    }
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token=0;
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok) { if(single_instance) CloseHandle(single_instance); if(SUCCEEDED(com)) CoUninitialize(); return 1; }
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_HOTKEY_CLASS|ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    POINT origin{}; scale=monitor_scale(GetDpiForSystem(),MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSW type{}; type.hInstance=instance; type.lpfnWndProc=procedure; type.lpszClassName=L"CrimeSQLRecovery";
    type.hCursor=LoadCursorW(nullptr,IDC_ARROW); type.hIcon=LoadIconW(nullptr,IDI_SHIELD); RegisterClassW(&type);
    auto hwnd=CreateWindowExW(WS_EX_COMPOSITED,type.lpszClassName,L"CrimeSQL dev system recovery",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,px(1240),px(860),nullptr,nullptr,instance,nullptr);
    if(!hwnd) { Gdiplus::GdiplusShutdown(token); if(single_instance) CloseHandle(single_instance); if(SUCCEEDED(com)) CoUninitialize(); return 1; }
    nh::apply_window_theme(hwnd);
    ShowWindow(hwnd,std::wstring(arguments)==L"--background"?SW_SHOWMINIMIZED:show);
    MSG message{}; int code=0;
    while((code=GetMessageW(&message,nullptr,0,0))>0) if(!IsDialogMessageW(hwnd,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    Gdiplus::GdiplusShutdown(token); if(SUCCEEDED(com)) CoUninitialize();
    if(single_instance) CloseHandle(single_instance);
    return code<0?1:static_cast<int>(message.wParam);
}
