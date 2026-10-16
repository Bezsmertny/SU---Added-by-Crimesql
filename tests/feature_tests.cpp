#include "recovery_features.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace { int checks=0; void check(bool value,const char* message) { ++checks; if(!value) throw std::runtime_error(message); } }
int main() {
    using namespace nh;
    try {
        check(valid_binding({3,0x77}),"Ctrl Alt F8");
        check(valid_binding({2,'A'}),"Ctrl A");
        check(!valid_binding({0,'A'}),"Do not intercept unmodified typing");
        check(!valid_binding({4,'A'}),"Shift alone is not enough");
        check(!valid_binding({10,'A'}),"Unsupported Windows modifier");
        check(!valid_binding({3,0x2e}),"Delete is not an allowed binding");
        check(valid_binding({3,0x87})&&!valid_binding({3,0x88}),"F24 boundary");
        check(same_binding({3,0x77},{3,0x77})&&!same_binding({3,0x77},{3,0x78}),"Binding comparison");
        check(autorun_command(L"C:\\Program Files\\CrimeSQL\\app.exe")==L"\"C:\\Program Files\\CrimeSQL\\app.exe\" --background","Quote autorun path");
        check(autorun_command(L"C:\\bad\".exe").empty()&&autorun_command(L"").empty(),"Reject invalid autorun path");
        const auto config=sandbox_config(L"C:\\Lab & 'test' <folder>");
        check(config.find(L"Lab &amp; &apos;test&apos; &lt;folder&gt;")!=std::wstring::npos,"Escape host folder XML");
        for(const auto* element:{L"Networking",L"ClipboardRedirection",L"AudioInput",L"VideoInput",L"PrinterRedirection"})
            check(config.find(std::wstring(L"<")+element+L">Disable</"+element+L">")!=std::wstring::npos,"Sandbox redirection disabled");
        check(config.find(L"<ReadOnly>true</ReadOnly>")!=std::wstring::npos,"Read-only mapping");
        check(config.find(L"<HostFolder>")==config.rfind(L"<HostFolder>"),"Only one curated host folder is mapped");
        check(config.find(L"observer.ready")<config.find(L"Start-Process &apos;C:\\CrimeSQLLab\\sample.exe&apos;"),"Observer readiness before sample");
        RenameTracker tracker;
        check(tracker.accept(4,L"old.exe").empty(),"Remember rename old path");
        auto pair=tracker.accept(5,L"new.exe");
        check(pair.size()==1&&pair[0].kind==FileEvent::Kind::renamed&&pair[0].path==L"old.exe"&&pair[0].destination==L"new.exe","Pair rename across buffers");
        check(tracker.accept(5,L"orphan")[0].kind==FileEvent::Kind::moved_in,"Do not invent old filename");
        tracker.accept(4,L"unpaired");
        auto interrupted=tracker.accept(1,L"created");
        check(interrupted.size()==2&&interrupted[0].kind==FileEvent::Kind::moved_out&&interrupted[1].kind==FileEvent::Kind::added,"Intervening event does not falsely rename");
        check(tracker.accept(2,L"deleted")[0].kind==FileEvent::Kind::removed,"Deletion");
        check(tracker.accept(3,L"changed")[0].kind==FileEvent::Kind::modified,"Modification");
        check(tracker.accept(99,L"unknown")[0].kind==FileEvent::Kind::lost,"Unknown event reported");
        MemoryGame game; game.reset(42);
        std::array<int,8> counts{};
        for(int i=0;i<16;++i) { ++counts.at(static_cast<std::size_t>(game.value(i))); check(!game.visible(i),"Initially hidden"); }
        check(std::all_of(counts.begin(),counts.end(),[](int n){return n==2;}),"Exactly eight pairs");
        check(!game.choose(-1)&&!game.choose(16),"Invalid card rejected");
        check(game.choose(0)&&!game.choose(0),"Cannot choose same card twice");
        int other=1; while(game.value(other)==game.value(0)) ++other;
        check(game.choose(other)&&game.waiting()&&game.moves()==1,"Mismatch holds two cards");
        check(!game.choose(15),"Third card blocked until conceal");
        game.conceal(); check(!game.visible(0)&&!game.visible(other)&&!game.waiting(),"Mismatch concealed");
        game.reset(42);
        for(int value=0;value<8;++value) {
            for(int i=0;i<16;++i) if(game.value(i)==value) check(game.choose(i),"Choose matching pair");
            check(game.found()==value+1&&!game.waiting(),"Matched pair remains visible");
        }
        check(game.found()==8&&game.moves()==8,"Game completion");
        check(!game.choose(0),"Matched cards cannot be replayed");
        game.reset(7); check(game.found()==0&&game.moves()==0&&!game.visible(0),"Reset game state");
        std::cout<<checks<<" feature checks passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"Check "<<checks<<" failed: "<<error.what()<<'\n'; return 1; }
}
