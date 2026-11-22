#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace oracles {
struct RomImage {
    std::vector<std::uint8_t> bytes;
    std::string title;
    std::uint8_t cartridge_type{};
    std::uint8_t rom_size_code{};
    std::uint8_t ram_size_code{};
    std::uint8_t cgb_flag{};
    std::size_t bank_count{};

    static RomImage load(const std::filesystem::path& path);
    std::uint8_t read(std::uint16_t address, std::uint16_t bank) const;
    std::size_t physical_offset(std::uint16_t address, std::uint16_t bank) const;
};

std::string sanitize_identifier(const std::string& input);
std::string sha256_file(const std::filesystem::path& path);
}
