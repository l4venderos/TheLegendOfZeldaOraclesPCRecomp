#pragma once

#include "oracles/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace oracles {

enum class FrameRunStatus {
    Running,      // The CPU slice completed; the PPU has not reached VBlank yet.
    FrameReady,   // A complete visible PPU frame is available in Runtime::ppu.
    Faulted       // An illegal/unhandled instruction or safety limit stopped execution.
};

// Owns the live cartridge execution path shared by the Windows frontend and
// portable integration tests. Native compiled blocks are dispatched first;
// dynamic/uncovered code addresses use the in-tree SM83 instruction fallback.
class GameRunner {
public:
    bool load(const std::filesystem::path& rom_path, NativeStep native_step,
              std::string& error);

    FrameRunStatus run_slice(std::uint8_t joypad_pressed, std::string& error,
                             std::size_t operation_limit = 300000);

    Runtime& runtime() noexcept { return runtime_; }
    const Runtime& runtime() const noexcept { return runtime_; }
    bool loaded() const noexcept { return loaded_; }
    bool running() const noexcept { return running_; }
    void stop() noexcept { running_ = false; }

    std::uint64_t frame_count() const noexcept { return frame_count_; }
    std::uint64_t native_block_count() const noexcept { return native_block_count_; }
    std::uint64_t interpreted_instruction_count() const noexcept { return interpreted_instruction_count_; }

private:
    static constexpr std::uint64_t kCpuCyclesPerFrame = 70224;

    Runtime runtime_;
    NativeStep native_step_{};
    bool loaded_{};
    bool running_{};
    std::uint64_t frame_count_{};
    std::uint64_t native_block_count_{};
    std::uint64_t interpreted_instruction_count_{};
};

} // namespace oracles
