#pragma once
#include <cstdint>
#include <string>

namespace oracles {
struct DecodedInstruction {
    std::uint16_t address{};
    std::uint8_t opcode{};
    std::uint8_t extended_opcode{};
    std::uint8_t length{1};
    std::uint8_t immediate8{};
    std::uint16_t immediate16{};
    bool is_cb{};
    bool legal{true};
};

DecodedInstruction decode_sm83(std::uint16_t address, std::uint8_t op,
                                std::uint8_t b1, std::uint8_t b2);
std::string disassemble_sm83(const DecodedInstruction& ins);
bool is_terminator(const DecodedInstruction& ins);
}
