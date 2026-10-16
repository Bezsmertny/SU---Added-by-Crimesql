#include "windows_features.h"
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
#include <vector>

namespace {
constexpr COLORREF bg=RGB(10,13,21), side=RGB(13,17,27), panel=RGB(19,25,38), line=RGB(36,46,65);
constexpr COLORREF ink=RGB(235,241,251), muted=RGB(140,155,179), teal=RGB(85,230,193), violet=RGB(162,141,255), amber=RGB(255,200,117);
constexpr int nav_base=100, scan_id=200, fix_base=210, mouse_id=220, keyboard_id=221, tools_base=230;
constexpr int defender_id=240, lab_id=250, autorun_id=260, priority_id=261, animate_id=262, bind_id=263, copy_id=270, reset_id=280, game_base=300;
constexpr int reaction_id=281;
HWND window=nullptr;
std::map<int,HWND> controls;
std::map<int,bool> hovered;
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
LRESULT CALLBACK button_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR ref) {
    int id=static_cast<int>(ref);
    if(id>=400&&id<403&&(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)) {
        unsigned modifiers=((GetKeyState(VK_CONTROL)&0x8000)?MOD_CONTROL:0)|((GetKeyState(VK_MENU)&0x8000)?MOD_ALT:0)|((GetKeyState(VK_SHIFT)&0x8000)?MOD_SHIFT:0);
        nh::Binding candidate{modifiers,static_cast<unsigned>(w)};
        if(nh::valid_binding(candidate)) { pending_keys[static_cast<std::size_t>(id-400)]=candidate; SetWindowTextW(h,nh::binding_name(candidate).c_str()); }
        return 0;
    }
    if(m==WM_MOUSEMOVE) { TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0}; TrackMouseEvent(&t); hovered[id]=true; InvalidateRect(h,nullptr,FALSE); }
    else if(m==WM_MOUSELEAVE) { hovered[id]=false; InvalidateRect(h,nullptr,FALSE); }
    else if(m==WM_NCDESTROY) RemoveWindowSubclass(h,button_proc,1);
    return DefSubclassProc(h,m,w,l);
}
void place(int id,int x,int y,int w,int h) {
    ShowWindow(controls.at(id),SW_SHOW); MoveWindow(controls.at(id),px(x),px(y),px(w),px(h),TRUE);
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
    for(auto& entry:controls) ShowWindow(entry.second,SW_HIDE);
    for(int i=0;i<7;++i) { place(nav_base+i,18,181+i*48,200,42); InvalidateRect(controls.at(nav_base+i),nullptr,FALSE); }
    for(auto h:key_fields) ShowWindow(h,page==4?SW_SHOW:SW_HIDE);
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
        for(int i=0;i<5;++i) place(tools_base+i,x+c-154,184+i*84,130,38);
        place(defender_id,x,623,c,45);
    } else if(page==4) {
        place(autorun_id,x+c-170,181,145,38); place(priority_id,x+c-170,275,145,38); place(animate_id,x+c-170,369,145,38);
        SetWindowTextW(controls.at(autorun_id),nh::autorun_enabled()?L"Отключить":L"Включить");
        SetWindowTextW(controls.at(priority_id),settings.high_priority?L"Обычный":L"Высокий");
        SetWindowTextW(controls.at(animate_id),settings.animations?L"Выключить":L"Включить");
        for(int i=0;i<3;++i) { MoveWindow(key_fields[i],px(x+c-275),px(483+i*51),px(250),px(34),TRUE); SendMessageW(key_fields[i],WM_SETFONT,reinterpret_cast<WPARAM>(font(14,FW_NORMAL)),TRUE); }
        place(bind_id,x+22,652,300,42);
    } else if(page==5) {
        for(int i=0;i<16;++i) place(game_base+i,x+22+(i%4)*110,192+(i/4)*100,96,86);
        place(reset_id,x+490,377,c-514,42);
        place(reaction_id,x+490,545,c-514,44);
    } else place(copy_id,x,height-112,240,42);
    InvalidateRect(window,nullptr,FALSE);
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
    std::wstring s=L"CrimeSQL dev system recovery 0.2.0\r\n";
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
    if(id>=nav_base&&id<nav_base+7) { nav_from=nav_position; nav_started=GetTickCount64(); page=id-nav_base; layout(); return; }
    if(id==scan_id) diagnose();
    else if(id>=fix_base&&id<fix_base+4) restore(static_cast<std::size_t>(id-fix_base));
    else if(id==mouse_id) result(nh::recover_mouse());
    else if(id==keyboard_id) {
        if(MessageBoxW(window,L"Отключить фильтрацию клавиш на текущий сеанс и запросить освобождение ввода? Чужие перехватчики это не удаляет.",L"Восстановление клавиатуры",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES) result(nh::recover_keyboard());
    } else if(id>=tools_base&&id<tools_base+5) {
        if(id==tools_base+1) task_manager();
        else { std::wstring error; if(!nh::open_tool(window,static_cast<nh::Tool>(id-tools_base),error)) result({false,error}); else log(L"Запрос открытия системного инструмента передан Windows."); }
    } else if(id==defender_id) {
        if(scan_process) { result({false,L"Команда проверки уже выполняется. Состояние смотрите в Безопасности Windows."}); return; }
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
    else if(id==reset_id) { game.reset(static_cast<unsigned>(GetTickCount64())); conceal_at=0; layout(); }
    else if(id==reaction_id) {
        auto now=GetTickCount64();
        if(!reaction_at) { reaction_at=now+1500+(now%2500); reaction_ready=false; reaction_score=L"Жди сигнала… Не нажимай раньше времени."; SetWindowTextW(controls.at(reaction_id),L"Жди сигнала…"); }
        else {
            reaction_score=now<reaction_at?L"Слишком рано! Попробуй ещё раз.":L"Твоя реакция: "+std::to_wstring(now-reaction_at)+L" мс";
            reaction_at=0; reaction_ready=false; SetWindowTextW(controls.at(reaction_id),L"Начать тест реакции");
        }
        InvalidateRect(window,nullptr,FALSE);
    }
    else if(id>=game_base&&id<game_base+16&&game.choose(id-game_base)) {
        revealed_at[static_cast<std::size_t>(id-game_base)]=GetTickCount64();
        if(game.waiting()) conceal_at=GetTickCount64()+850;
        for(int i=0;i<16;++i) InvalidateRect(controls.at(game_base+i),nullptr,FALSE);
        if(game.found()==8) log(L"Все пары найдены! Ходов: "+std::to_wstring(game.moves()));
        InvalidateRect(window,nullptr,FALSE);
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
    shape(dc,22,108,192,31,RGB(20,39,38),RGB(35,65,58),18); text(dc,L"●  LOCAL-FIRST / v0.2.0",36,110,173,26,10,teal,FW_SEMIBOLD);
    text(dc,L"РАБОЧЕЕ ПРОСТРАНСТВО",24,150,190,20,10,muted,FW_SEMIBOLD);
    shape(dc,18,181+static_cast<int>(nav_position*48),200,42,RGB(42,35,67),RGB(66,53,102),14);
    shape(dc,18,height-172,200,100,panel); text(dc,L"АВАРИЙНЫЙ ВХОД",32,height-160,170,19,10,muted,FW_BOLD);
    text(dc,L"Ctrl + Alt + Del",32,height-135,170,25,16,teal,FW_SEMIBOLD);
    text(dc,L"Если ввод полностью заблокирован",32,height-104,170,30,10,muted,FW_NORMAL,true);
    text(dc,L"CrimeSQL dev • Windows 10 / 11",24,height-43,196,24,10,muted);
    const wchar_t* titles[]{L"Верни контроль.",L"Восстановление доступа",L"Лаборатория EXE",L"Системные инструменты",L"Твой центр управления",L"Перерыв: найди пары",L"Журнал действий"};
    const wchar_t* captions[]{L"Диагностика, восстановление и изолированный анализ — в одном месте.",L"Распознанные ограничения профиля. Изменения — с резервной копией.",L"Наблюдай за изменениями файлов внутри Windows Sandbox.",L"Средства Windows для проверки и восстановления системы.",L"Горячие клавиши, автозапуск и поведение приложения.",L"Восемь пар. Никаких сетевых сервисов, только немного отдыха.",L"История сеанса: до 2000 записей, без отправки на сервер."};
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
        const wchar_t* names[]{L"Безопасность Windows",L"Диспетчер задач",L"Сведения о системе",L"Восстановление Windows",L"Резервные копии .reg"};
        const wchar_t* notes[]{L"Защита, журнал обнаружений и автономная проверка.",L"Восстановить ограничение и открыть процессы.",L"Оборудование и компоненты Windows.",L"Открыть параметры восстановления системы.",L"Исходные значения перед изменением политик."};
        for(int i=0;i<5;++i) { shape(dc,x,167+i*84,c,72,panel); text(dc,names[i],x+20,177+i*84,c-205,27,16,ink,FW_SEMIBOLD); text(dc,notes[i],x+20,208+i*84,c-205,20,12,muted); }
        text(dc,scan_process?L"Команда проверки запущена. Итог смотри в Безопасности Windows.":L"Полная проверка использует Microsoft Defender и может запросить права администратора.",x,687,c,47,12,muted,FW_NORMAL,true);
    } else if(page==4) {
        const wchar_t* names[]{L"Запускать вместе с Windows",L"Приоритет приложения",L"Плавные анимации"};
        std::wstring notes[]{nh::autorun_enabled()?L"Включён • для текущего пользователя":L"Выключен • запуск свёрнутым после входа",settings.high_priority?L"Высокий • только процесс CrimeSQL":L"Обычный • рекомендуемый режим",settings.animations?L"Включены • движение акцентов":L"Выключены • уменьшенное движение"};
        for(int i=0;i<3;++i) { shape(dc,x,164+i*94,c,80,panel); text(dc,names[i],x+20,176+i*94,c-220,27,17,ink,FW_SEMIBOLD); text(dc,notes[i],x+20,209+i*94,c-220,22,12,muted); }
        shape(dc,x,459,c,250,panel);
        const wchar_t* labels[]{L"Освободить мышь",L"Восстановить клавиатуру",L"Восстановить / открыть диспетчер"};
        for(int i=0;i<3;++i) { text(dc,labels[i],x+22,482+i*51,c-320,24,14,ink,FW_SEMIBOLD); text(dc,bound[i]?L"Бинд зарегистрирован":L"Бинд недоступен — выбери другой",x+22,506+i*51,c-320,17,10,bound[i]?teal:amber); }
        text(dc,L"Нажми сочетание в поле: Ctrl/Alt + буква, цифра или F1–F24. Бинды работают, пока приложение запущено.",x,725,c,38,11,muted,FW_NORMAL,true);
    } else if(page==5) {
        shape(dc,x,168,466,430,panel); text(dc,L"MEMORY BREAK",x+490,192,c-510,24,11,teal,FW_BOLD);
        text(dc,std::to_wstring(game.found())+L" / 8",x+490,237,c-510,55,40,ink,FW_BOLD); text(dc,L"пар найдено",x+490,293,c-510,26,14,muted);
        text(dc,L"Ходов: "+std::to_wstring(game.moves()),x+490,337,c-510,25,16,violet,FW_SEMIBOLD);
        text(dc,game.found()==8?L"Все пары найдены. Отличная работа!":L"Открывай две карточки и запоминай совпадения.",x+490,447,c-510,72,14,muted,FW_NORMAL,true);
        text(dc,reaction_score,x+490,603,c-510,50,13,teal,FW_NORMAL,true);
        text(dc,L"Мини-игры не изменяют настройки системы и не влияют на защиту.",x,686,c,36,12,muted);
    } else {
        shape(dc,x,165,c,height-306,panel);
        auto capacity=static_cast<std::size_t>(std::max(1,(height-344)/49)); auto first=journal.size()>capacity?journal.size()-capacity:0; int y=180;
        for(std::size_t i=first;i<journal.size();++i,y+=49) text(dc,journal[i],x+20,y,c-40,45,12,muted,FW_NORMAL,true);
        text(dc,L"Кнопка копирует весь журнал. Пути могут содержать имя пользователя.",x+260,height-110,c-260,44,11,muted,FW_NORMAL,true);
    }
    text(dc,L"CrimeSQL dev system recovery / LOCAL TOOLS. CLEAR ACTIONS.",x,height-33,c,19,10,muted);
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
        case WM_CREATE: {
            window=hwnd; scale=monitor_scale(GetDpiForWindow(hwnd),MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST));
            settings=nh::load_settings(); started=GetTickCount64();
            const wchar_t* nav[]{L"01   Панель управления",L"02   Восстановление",L"03   Лаборатория EXE",L"04   Инструменты",L"05   Настройки и бинды",L"06   Перерыв",L"07   Журнал действий"};
            for(int i=0;i<7;++i) button(nav_base+i,nav[i]);
            button(scan_id,L"Проверить систему  →"); for(int i=0;i<4;++i) button(fix_base+i,L"Восстановить");
            button(mouse_id,L"Освободить мышь"); button(keyboard_id,L"Восстановить клавиатуру");
            for(int i=0;i<5;++i) button(tools_base+i,L"Открыть  ↗");
            button(defender_id,L"Запустить полную проверку Microsoft Defender"); button(lab_id,L"Выбрать EXE и открыть Sandbox  ↗");
            button(autorun_id,L"Включить"); button(priority_id,L"Высокий"); button(animate_id,L"Выключить"); button(bind_id,L"Сохранить и применить бинды");
            button(copy_id,L"Копировать весь журнал"); button(reset_id,L"Новая игра"); for(int i=0;i<16;++i) button(game_base+i,L"Карточка");
            button(reaction_id,L"Начать тест реакции");
            for(auto& entry:controls) SetWindowSubclass(entry.second,button_proc,1,static_cast<DWORD_PTR>(entry.first));
            for(int i=0;i<3;++i) { key_fields[static_cast<std::size_t>(i)]=button(400+i,L"Нажмите сочетание"); SetWindowSubclass(key_fields[static_cast<std::size_t>(i)],button_proc,1,400+i); }
            set_key_fields(); if(!register_keys(settings.bindings)) log(L"Некоторые горячие клавиши заняты. Измените их в Настройках.");
            if(settings.high_priority) { auto r=nh::set_high_priority(true); if(!r.success) { settings.high_priority=false; log(r.message); } }
            BOOL animation=TRUE; if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0)&&!animation) settings.animations=false;
            machine=nh::system_summary(); game.reset(static_cast<unsigned>(GetTickCount64()));
            log(L"CrimeSQL 0.2.0 запущен. Восстановление без команды пользователя не выполняется.");
            SetTimer(hwnd,1,33,nullptr); layout(); return 0;
        }
        case WM_TIMER: {
            auto now=GetTickCount64(); double progress=settings.animations?std::min(1.0,(now-nav_started)/220.0):1.0;
            double previous=nav_position; nav_position=nav_from+(page-nav_from)*(1-std::pow(1-progress,3));
            if(previous!=nav_position||(settings.animations&&page==0&&GetForegroundWindow()==hwnd)) InvalidateRect(hwnd,nullptr,FALSE);
            if(conceal_at&&now>=conceal_at) { game.conceal(); conceal_at=0; for(int i=0;i<16;++i) InvalidateRect(controls.at(game_base+i),nullptr,FALSE); }
            if(reaction_at&&!reaction_ready&&now>=reaction_at) { reaction_ready=true; SetWindowTextW(controls.at(reaction_id),L"СЕЙЧАС! Нажми"); }
            if(settings.animations&&page==5) for(int i=0;i<16;++i) if(revealed_at[static_cast<std::size_t>(i)]&&now-revealed_at[static_cast<std::size_t>(i)]<190) InvalidateRect(controls.at(game_base+i),nullptr,FALSE);
            if(scan_process) {
                DWORD code=STILL_ACTIVE;
                if(GetExitCodeProcess(scan_process,&code)&&code!=STILL_ACTIVE) {
                    CloseHandle(scan_process); scan_process=nullptr;
                    log(code==0?L"Команда проверки Defender завершена. Обнаружения смотрите в Безопасности Windows.":L"Команда Defender завершилась с ошибкой "+std::to_wstring(code)+L". Откройте Безопасность Windows.");
                }
            }
            return 0;
        }
        case WM_HOTKEY: {
            int focused=GetDlgCtrlID(GetFocus());
            if(focused>=400&&focused<403&&w>=1&&w<=3) {
                pending_keys[static_cast<std::size_t>(focused-400)]=settings.bindings[static_cast<std::size_t>(w-1)];
                SetWindowTextW(GetFocus(),nh::binding_name(pending_keys[static_cast<std::size_t>(focused-400)]).c_str());
            } else if(w==1) command(mouse_id); else if(w==2) command(keyboard_id); else if(w==3) task_manager();
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={px(1200),px(850)}; return 0;
        case WM_DPICHANGED: {
            scale=monitor_scale(HIWORD(w),MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST)); clear_fonts();
            auto rect=reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,rect->left,rect->top,px(1240),px(860),SWP_NOZORDER|SWP_NOACTIVATE); layout(); return 0;
        }
        case WM_COMMAND: if(HIWORD(w)==BN_CLICKED) command(LOWORD(w)); return 0;
        case WM_DRAWITEM: {
            auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l); int id=static_cast<int>(item->CtlID),width=logical(item->rcItem.right),height=logical(item->rcItem.bottom);
            bool nav=id>=nav_base&&id<nav_base+7,active=nav&&id-nav_base==page,primary=id==scan_id||id==lab_id||id==bind_id;
            bool card=id>=game_base&&id<game_base+16;
            bool inside_panel=card||(page==1&&id>=fix_base&&id<fix_base+4)||(page==2&&id==lab_id)||(page==3&&id>=tools_base&&id<tools_base+5)||page==4;
            COLORREF behind=nav?side:(page==0&&id==scan_id)?RGB(23,27,45):inside_panel?panel:bg;
            auto brush=CreateSolidBrush(behind); FillRect(item->hDC,&item->rcItem,brush); DeleteObject(brush);
            COLORREF fill=primary?teal:active?RGB(42,35,67):hovered[id]?RGB(36,43,61):id>=400?RGB(27,34,51):nav?side:panel;
            if(item->itemState&ODS_SELECTED) fill=RGB(49,59,80);
            shape(item->hDC,0,0,width,height,fill,active?RGB(74,59,116):id>=400?line:fill,14);
            wchar_t label[256]{}; GetWindowTextW(item->hwndItem,label,256); std::wstring title=label; int size=13,margin=15;
            if(id>=game_base&&id<game_base+16) {
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
            }
            text(item->hDC,title,margin,0,width-25,height,size,(item->itemState&ODS_DISABLED)?RGB(76,91,112):primary?bg:(active||card||id>=400)?violet:ink,FW_SEMIBOLD);
            if(item->itemState&ODS_FOCUS) { auto area=item->rcItem; InflateRect(&area,-px(4),-px(4)); DrawFocusRect(item->hDC,&area); }
            return TRUE;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); RECT r{}; GetClientRect(hwnd,&r);
            if(r.right>0&&r.bottom>0) {
                HDC buffer=CreateCompatibleDC(dc); auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom); auto old=SelectObject(buffer,bitmap);
                paint(buffer); BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY); SelectObject(buffer,old); DeleteObject(bitmap); DeleteDC(buffer);
            }
            EndPaint(hwnd,&ps); return 0;
        }
        case WM_DESTROY:
            for(int i=1;i<=3;++i) UnregisterHotKey(hwnd,i);
            if(scan_process) CloseHandle(scan_process);
            clear_fonts(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int show) {
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
    auto hwnd=CreateWindowExW(0,type.lpszClassName,L"CrimeSQL dev system recovery",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,px(1240),px(860),nullptr,nullptr,instance,nullptr);
    if(!hwnd) { Gdiplus::GdiplusShutdown(token); if(single_instance) CloseHandle(single_instance); if(SUCCEEDED(com)) CoUninitialize(); return 1; }
    nh::apply_window_theme(hwnd);
    ShowWindow(hwnd,std::wstring(arguments)==L"--background"?SW_SHOWMINIMIZED:show);
    MSG message{}; int code=0;
    while((code=GetMessageW(&message,nullptr,0,0))>0) if(!IsDialogMessageW(hwnd,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    Gdiplus::GdiplusShutdown(token); if(SUCCEEDED(com)) CoUninitialize();
    if(single_instance) CloseHandle(single_instance);
    return code<0?1:static_cast<int>(message.wParam);
}
