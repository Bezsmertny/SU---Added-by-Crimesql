#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace nh {
struct Policy {
    const wchar_t* name;
    const wchar_t* description;
    const wchar_t* key;
    const wchar_t* value;
    std::uint32_t max_block_value;
};
extern const std::array<Policy, 4> policies;
enum class ReadState { missing, dword, unsupported_type, denied, error };
struct Observation {
    ReadState state = ReadState::error;
    std::uint32_t value = 0;
    std::uint32_t error = 0;
};
enum class Health { clear, blocked, unknown };
Health classify(const Policy& policy, const Observation& observation);
std::wstring registry_backup(const Policy& policy, std::uint32_t original);
}
