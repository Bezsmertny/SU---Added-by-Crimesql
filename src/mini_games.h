#pragma once
#include <array>
#include <random>
#include <vector>

namespace nh {
class TicTacToe {
    std::array<int,9> cells{};
    static int outcome(const std::array<int,9>& board);
    static int minimax(std::array<int,9>& board,bool computer,int depth);
public:
    void reset() { cells.fill(0); }
    bool play(int index);
    int value(int index) const { return cells.at(static_cast<std::size_t>(index)); }
    int status() const { return outcome(cells); }
};
class Puzzle15 {
    std::array<int,16> cells{};
    int blank=15,turns=0;
public:
    void reset(unsigned seed);
    bool slide(int index);
    int value(int index) const { return cells.at(static_cast<std::size_t>(index)); }
    int moves() const { return turns; }
    bool solved() const;
    static bool solvable(const std::array<int,16>& board);
};
class LightsOut {
    std::array<bool,25> cells{};
    int turns=0;
public:
    void reset(unsigned seed);
    bool toggle(int index);
    bool value(int index) const { return cells.at(static_cast<std::size_t>(index)); }
    bool solved() const;
    int moves() const { return turns; }
};
class SimonGame {
    std::vector<int> notes;
    std::mt19937 random;
    std::size_t position=0;
    bool playback=true,failed=false;
public:
    enum class Answer { ignored,correct,round_complete,wrong };
    void reset(unsigned seed);
    void allow_input() { if(!failed) { playback=false; position=0; } }
    Answer accept(int index);
    const std::vector<int>& sequence() const { return notes; }
    bool showing() const { return playback&&!failed; }
    bool lost() const { return failed; }
};
}
