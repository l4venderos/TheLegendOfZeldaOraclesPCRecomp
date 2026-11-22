#include "oracles/game_runner.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace oracles::generated {
std::uint16_t ages_step(oracles::Runtime&, std::uint16_t, std::uint16_t);
std::uint16_t seasons_step(oracles::Runtime&, std::uint16_t, std::uint16_t);
std::size_t ages_block_count();
std::size_t seasons_block_count();
}

namespace {
struct GameSpec {
    const char* id;
    const char* title;
    const char* rom;
    oracles::NativeStep step;
    std::size_t blocks;
};

bool save_ppm(const fs::path& output, const oracles::Ppu& ppu, std::string& error) {
    std::ofstream file(output, std::ios::binary);
    if (!file) { error = "Cannot create framebuffer image: " + output.string(); return false; }
    file << "P6\n" << oracles::Ppu::kScreenWidth << ' ' << oracles::Ppu::kScreenHeight << "\n255\n";
    for (const auto pixel : ppu.framebuffer()) {
        const char rgb[3] = {
            static_cast<char>(pixel & 0xFF),
            static_cast<char>((pixel >> 8) & 0xFF),
            static_cast<char>((pixel >> 16) & 0xFF)
        };
        file.write(rgb, sizeof(rgb));
    }
    if (!file) { error = "Failed while writing framebuffer image: " + output.string(); return false; }
    return true;
}

bool run_game(const GameSpec& game, const fs::path& data_dir,
              const fs::path& output_dir, unsigned frames) {
    const fs::path rom = data_dir / game.rom;
    oracles::GameRunner runner;
    std::string error;
    if (!runner.load(rom, game.step, error)) {
        std::cerr << game.title << ": " << error << '\n';
        return false;
    }

    std::cout << game.title << ": " << game.blocks << " compiled native blocks; starting at 0100h\n";
    unsigned saved = 0;
    for (unsigned slices = 0; saved < frames && slices < frames * 240u + 240u; ++slices) {
        const auto result = runner.run_slice(0, error);
        if (result == oracles::FrameRunStatus::Faulted) {
            std::cerr << game.title << " stopped: " << error << '\n';
            return false;
        }
        if (result == oracles::FrameRunStatus::FrameReady) {
            ++saved;
            std::ostringstream name;
            name << game.id << "-frame-" << std::setw(6) << std::setfill('0') << saved << ".ppm";
            if (!save_ppm(output_dir / name.str(), runner.runtime().ppu, error)) {
                std::cerr << error << '\n';
                return false;
            }
            std::cout << "  frame " << saved << ": PC=0x" << std::hex << runner.runtime().cpu.pc
                      << std::dec << ", native blocks=" << runner.native_block_count()
                      << ", interpreter instructions=" << runner.interpreted_instruction_count() << '\n';
        }
    }
    if (saved != frames) {
        std::cerr << game.title << ": did not produce " << frames << " complete PPU frames\n";
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    std::string selected = "both";
    unsigned frames = 1;
    fs::path data_dir = "GameData";
    fs::path output_dir = "frames";

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("Missing value after " + arg);
                return argv[++i];
            };
            if (arg == "--game") selected = next();
            else if (arg == "--frames") frames = static_cast<unsigned>(std::stoul(next()));
            else if (arg == "--data-dir") data_dir = next();
            else if (arg == "--output-dir") output_dir = next();
            else if (arg == "--help" || arg == "-h") {
                std::cout << "Oracles PC Recomp headless renderer\n"
                          << "Usage: Oracles [--game ages|seasons|both] [--frames N] "
                          << "[--data-dir path] [--output-dir path]\n"
                          << "Runs the cartridge CPU and PPU, then writes actual RGBA framebuffer frames as PPM.\n";
                return 0;
            } else throw std::runtime_error("Unknown argument: " + arg);
        }
        if (selected != "both" && selected != "ages" && selected != "seasons")
            throw std::runtime_error("--game must be ages, seasons or both");
        if (frames == 0 || frames > 10000) throw std::runtime_error("--frames must be between 1 and 10000");
        fs::create_directories(output_dir);

        const GameSpec ages{"ages", "Oracle of Ages", "OracleOfAges.gbc",
            oracles::generated::ages_step, oracles::generated::ages_block_count()};
        const GameSpec seasons{"seasons", "Oracle of Seasons", "OracleOfSeasons.gbc",
            oracles::generated::seasons_step, oracles::generated::seasons_block_count()};
        bool ok = true;
        if (selected == "both" || selected == "ages") ok = run_game(ages, data_dir, output_dir, frames) && ok;
        if (selected == "both" || selected == "seasons") ok = run_game(seasons, data_dir, output_dir, frames) && ok;
        if (ok) std::cout << "Framebuffer output written to: " << fs::absolute(output_dir).string() << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "[error] " << ex.what() << '\n';
        return 2;
    }
}
