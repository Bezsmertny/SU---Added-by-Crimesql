#include "mini_games.h"
#include "recovery_actions.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace {
int checks=0,terminal_games=0;
void check(bool value,const char* message) { ++checks; if(!value) throw std::runtime_error(message); }
void exhaust_human_choices(nh::TicTacToe game) {
    check(game.status()!=1,"Computer never loses to any legal sequence of human moves");
    if(game.status()!=0) { ++terminal_games; return; }
    for(int i=0;i<9;++i) if(game.value(i)==0) {
        auto next=game; check(next.play(i),"Legal human move accepted"); exhaust_human_choices(next);
    }
}
}
int main(int argc,char** argv) {
    try {
        nh::TicTacToe tic;
        check(!tic.play(-1)&&!tic.play(9),"Reject invalid board positions");
        exhaust_human_choices(tic);
        check(terminal_games>100,"Exhaustive tic tac toe reached many terminal games");
        tic.play(4); check(!tic.play(4),"Occupied tic tac toe square rejected"); tic.reset(); check(tic.value(4)==0,"Reset tic tac toe");
        std::array<int,16> solved{}; std::iota(solved.begin(),solved.end(),1); solved[15]=0;
        check(nh::Puzzle15::solvable(solved),"Solved puzzle is solvable");
        std::swap(solved[0],solved[1]); check(!nh::Puzzle15::solvable(solved),"Impossible parity rejected");
        solved[0]=solved[1]; check(!nh::Puzzle15::solvable(solved),"Duplicate tiles rejected");
        for(unsigned seed=0;seed<100;++seed) {
            nh::Puzzle15 puzzle; puzzle.reset(seed); std::array<int,16> board{}; int empty=0;
            for(int i=0;i<16;++i) { board[static_cast<std::size_t>(i)]=puzzle.value(i); if(puzzle.value(i)==0) empty=i; }
            check(nh::Puzzle15::solvable(board)&&!puzzle.solved()&&puzzle.moves()==0,"Generated puzzle is valid, solvable and unsolved");
            check(!puzzle.slide(empty)&&!puzzle.slide(16),"Invalid puzzle move leaves state intact");
            const int neighbor=empty%4==0?empty+1:empty-1;
            check(puzzle.slide(neighbor)&&puzzle.moves()==1&&puzzle.value(neighbor)==0,"Adjacent tile moves into empty square");
            nh::LightsOut lights; lights.reset(seed); std::array<bool,25> before{};
            for(int i=0;i<25;++i) before[static_cast<std::size_t>(i)]=lights.value(i);
            check(!lights.solved()&&lights.moves()==0,"Lights Out begins unsolved");
            check(!lights.toggle(-1)&&!lights.toggle(25),"Reject invalid light index");
            lights.toggle(0);
            for(int i=0;i<25;++i) check(lights.value(i)==(i==0||i==1||i==5?!before[static_cast<std::size_t>(i)]:before[static_cast<std::size_t>(i)]),"Corner toggles only itself and two valid neighbors");
            lights.toggle(0); for(int i=0;i<25;++i) check(lights.value(i)==before[static_cast<std::size_t>(i)],"Two identical light toggles restore board");
            std::mt19937 random(seed);
            for(int i=0;i<25;++i) lights.toggle(std::uniform_int_distribution<int>(0,24)(random));
            if(!lights.solved()) lights.toggle(12);
            check(lights.solved(),"Generated light puzzle can be undone to solved state");
        }
        nh::SimonGame simon; simon.reset(7);
        check(simon.accept(0)==nh::SimonGame::Answer::ignored,"No input during Simon playback");
        for(int round=1;round<=15;++round) {
            auto expected=simon.sequence(); check(expected.size()==static_cast<std::size_t>(round),"Simon grows one note per round");
            simon.allow_input();
            for(std::size_t i=0;i<expected.size();++i) check(simon.accept(expected[i])==(i+1==expected.size()?nh::SimonGame::Answer::round_complete:nh::SimonGame::Answer::correct),"Repeat Simon sequence");
            check(simon.showing()&&!simon.lost(),"Next round returns to playback");
        }
        simon.allow_input(); const int wrong=(simon.sequence()[0]+1)%4;
        check(simon.accept(wrong)==nh::SimonGame::Answer::wrong&&simon.lost(),"Wrong Simon note ends game");
        check(simon.accept(0)==nh::SimonGame::Answer::ignored,"No play after Simon loss");
        simon.reset(8); check(!simon.lost()&&simon.sequence().size()==1,"Simon reset clears loss");
        using A=nh::RecoveryAction;
        check(nh::powershell_quote(L"C:\\O'Brien\\app")==L"'C:\\O''Brien\\app'","PowerShell path quoting");
        check(nh::recovery_script(A::sfc_verify,L"C:\\Windows\\System32").find(L"/verifyonly")!=std::wstring::npos,"SFC verify command is read-only");
        check(nh::recovery_script(A::sfc_repair,L"C:\\Windows\\System32").find(L"/scannow")!=std::wstring::npos,"SFC repair command");
        check(nh::recovery_script(A::dism_restore,L"C:\\Windows\\System32").find(L"/Online /Cleanup-Image /RestoreHealth")!=std::wstring::npos,"DISM repair command");
        check(nh::recovery_script(A::winre_info,L"C:\\Windows\\System32").find(L"/info")!=std::wstring::npos,"WinRE info does not alter boot configuration");
        check(nh::recovery_script(A::defender_offline,L"C:\\Windows\\System32").find(L"Start-MpWDOScan")!=std::wstring::npos,"Use built-in Defender offline scan");
        check(nh::action_info(A::restore_fonts).confirmation[0],"Font recovery requires explicit confirmation");
        check(nh::recovery_category_count==5&&nh::recovery_category_size(4)==2,"Font tools have their own category without empty actions");
        for(int i=0;i<nh::recovery_action_count;++i) {
            auto action=static_cast<A>(i); const auto& info=nh::action_info(action);
            check(info.title[0]&&info.description[0],"Each recovery tool has an explanation");
            if(!nh::recovery_script(action,L"C:\\Windows\\System32").empty()) check(info.confirmation[0],"Maintenance operation requires confirmation");
            const auto script=nh::recovery_script(action,L"C:\\Windows\\System32");
            check(script.find(L"ExecutionPolicy")==std::wstring::npos&&script.find(L"DisableRealtimeMonitoring")==std::wstring::npos,"Do not bypass policy or disable antivirus");
        }
        if(argc==3&&std::string(argv[1])=="--export-scripts") {
            const auto directory=std::filesystem::path(argv[2]); std::filesystem::create_directories(directory);
            for(int i=0;i<nh::recovery_action_count;++i) {
                const auto script=nh::recovery_script(static_cast<A>(i),L"C:\\Windows\\System32");
                if(script.empty()) continue;
                const auto body=nh::maintenance_wrapper(script,L"C:\\Reports\\O'Brien\\Отчёты\\test.txt");
                std::ofstream file(directory/(std::to_string(i)+".ps1"),std::ios::binary);
                file.put(static_cast<char>(0xff)); file.put(static_cast<char>(0xfe));
                for(auto character:body) { file.put(static_cast<char>(character&0xff)); file.put(static_cast<char>((character>>8)&0xff)); }
                check(file.good(),"Export exact generated maintenance script for parser validation only");
            }
        }
        std::cout<<checks<<" game/recovery checks passed; "<<terminal_games<<" complete tic tac toe games explored\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<"Check "<<checks<<" failed: "<<e.what()<<'\n'; return 1; }
}
