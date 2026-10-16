#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nh {
struct Binding { unsigned modifiers; unsigned key; };
bool valid_binding(Binding binding);
bool same_binding(Binding a, Binding b);
std::wstring xml_escape(const std::wstring& value);
std::wstring sandbox_config(const std::wstring& input_folder);
std::wstring autorun_command(const std::wstring& executable);

struct FileEvent {
    enum class Kind { added, removed, modified, renamed, moved_out, moved_in, lost } kind;
    std::wstring path;
    std::wstring destination;
};
class RenameTracker {
    std::wstring old_name;
public:
    std::vector<FileEvent> accept(unsigned action, const std::wstring& name);
    std::vector<FileEvent> flush();
};
const wchar_t* event_name(FileEvent::Kind kind);

class MemoryGame {
    std::array<int, 16> cards{};
    std::array<bool, 16> matched{};
    int first = -1, second = -1, attempts = 0, pairs = 0;
public:
    void reset(unsigned seed);
    bool choose(int index);
    void conceal();
    bool visible(int index) const;
    int value(int index) const { return cards.at(static_cast<std::size_t>(index)); }
    int moves() const { return attempts; }
    int found() const { return pairs; }
    bool waiting() const { return second >= 0; }
};
}
