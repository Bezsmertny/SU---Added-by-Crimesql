#include "mini_games.h"
#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <utility>

namespace nh {
int TicTacToe::outcome(const std::array<int,9>& board) {
    constexpr int lines[][3]{{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    for(const auto& line:lines) if(board[line[0]]&&board[line[0]]==board[line[1]]&&board[line[1]]==board[line[2]]) return board[line[0]];
    return std::find(board.begin(),board.end(),0)==board.end()?3:0;
}
int TicTacToe::minimax(std::array<int,9>& board,bool computer,int depth) {
    const int state=outcome(board);
    if(state) return state==2?10-depth:state==1?depth-10:0;
    int best=computer?-100:100;
    for(auto& cell:board) if(!cell) {
        cell=computer?2:1; const int score=minimax(board,!computer,depth+1); cell=0;
        best=computer?std::max(best,score):std::min(best,score);
    }
    return best;
}
bool TicTacToe::play(int index) {
    if(index<0||index>=9||status()!=0||cells[static_cast<std::size_t>(index)]) return false;
    cells[static_cast<std::size_t>(index)]=1;
    if(status()!=0) return true;
    int best=-100,choice=-1;
    for(int i=0;i<9;++i) if(!cells[static_cast<std::size_t>(i)]) {
        cells[static_cast<std::size_t>(i)]=2; const int score=minimax(cells,false,0); cells[static_cast<std::size_t>(i)]=0;
        if(score>best) { best=score; choice=i; }
    }
    if(choice>=0) cells[static_cast<std::size_t>(choice)]=2;
    return true;
}
void Puzzle15::reset(unsigned seed) {
    std::iota(cells.begin(),cells.end(),1); cells[15]=0; blank=15; turns=0;
    std::mt19937 random(seed); int previous=-1;
    for(int step=0;step<160;++step) {
        std::vector<int> choices;
        for(int i=0;i<16;++i) if(i!=previous&&std::abs(i/4-blank/4)+std::abs(i%4-blank%4)==1) choices.push_back(i);
        const int choice=choices[std::uniform_int_distribution<std::size_t>(0,choices.size()-1)(random)];
        previous=blank; slide(choice);
    }
    if(solved()) slide(blank%4==0?blank+1:blank-1);
    turns=0;
}
bool Puzzle15::slide(int index) {
    if(index<0||index>=16||std::abs(index/4-blank/4)+std::abs(index%4-blank%4)!=1) return false;
    std::swap(cells[static_cast<std::size_t>(blank)],cells[static_cast<std::size_t>(index)]); blank=index; ++turns; return true;
}
bool Puzzle15::solved() const {
    for(int i=0;i<15;++i) if(cells[static_cast<std::size_t>(i)]!=i+1) return false;
    return cells[15]==0;
}
bool Puzzle15::solvable(const std::array<int,16>& board) {
    auto sorted=board; std::sort(sorted.begin(),sorted.end());
    for(int i=0;i<16;++i) if(sorted[static_cast<std::size_t>(i)]!=i) return false;
    int inversions=0,blank_row=0;
    for(int i=0;i<16;++i) {
        if(board[static_cast<std::size_t>(i)]==0) blank_row=4-i/4;
        for(int j=i+1;j<16;++j) if(board[static_cast<std::size_t>(i)]&&board[static_cast<std::size_t>(j)]&&board[static_cast<std::size_t>(i)]>board[static_cast<std::size_t>(j)]) ++inversions;
    }
    return (inversions+blank_row)%2==1;
}
void LightsOut::reset(unsigned seed) {
    cells.fill(false); std::mt19937 random(seed);
    for(int i=0;i<25;++i) toggle(std::uniform_int_distribution<int>(0,24)(random));
    if(solved()) toggle(12);
    turns=0;
}
bool LightsOut::toggle(int index) {
    if(index<0||index>=25) return false;
    for(int i=0;i<25;++i) if(std::abs(i/5-index/5)+std::abs(i%5-index%5)<=1) cells[static_cast<std::size_t>(i)]=!cells[static_cast<std::size_t>(i)];
    ++turns; return true;
}
bool LightsOut::solved() const { return std::none_of(cells.begin(),cells.end(),[](bool lit){return lit;}); }
void SimonGame::reset(unsigned seed) {
    random.seed(seed); notes.assign(1,std::uniform_int_distribution<int>(0,3)(random)); position=0; playback=true; failed=false;
}
SimonGame::Answer SimonGame::accept(int index) {
    if(index<0||index>=4||playback||failed) return Answer::ignored;
    if(index!=notes[position]) { failed=true; return Answer::wrong; }
    if(++position<notes.size()) return Answer::correct;
    notes.push_back(std::uniform_int_distribution<int>(0,3)(random)); position=0; playback=true; return Answer::round_complete;
}
}
