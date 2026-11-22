#pragma once
#include "oracles/rom.hpp"
#include "oracles/sm83.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace oracles {
struct BankAddress {
    std::uint16_t bank{};
    std::uint16_t address{};
    bool operator==(const BankAddress&) const = default;
};
struct NativeBlock {
    BankAddress start;
    std::vector<DecodedInstruction> instructions;
};
struct AnalysisResult {
    std::vector<NativeBlock> blocks;
    std::size_t decoded_instructions{};
    std::size_t rejected_paths{};
};
AnalysisResult analyze_rom(const RomImage& rom, std::size_t max_blocks = 12000,
                           std::size_t max_instructions_per_block = 32);
}
