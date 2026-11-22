#include "oracles/game_runner.hpp"

#include <iomanip>
#include <sstream>

namespace oracles {

bool GameRunner::load(const std::filesystem::path& rom_path, NativeStep native_step,
                      std::string& error) {
    stop();
    loaded_ = false;
    native_step_ = nullptr;
    frame_count_ = 0;
    native_block_count_ = 0;
    interpreted_instruction_count_ = 0;

    if (!runtime_.load_rom(rom_path, error)) return false;
    native_step_ = native_step;
    loaded_ = true;
    running_ = true;
    error.clear();
    return true;
}

FrameRunStatus GameRunner::run_slice(std::uint8_t joypad_pressed, std::string& error,
                                     std::size_t operation_limit) {
    error.clear();
    if (!loaded_ || !running_) {
        error = "No running cartridge is loaded.";
        return FrameRunStatus::Faulted;
    }
    if (operation_limit == 0) {
        error = "The instruction operation limit must be greater than zero.";
        running_ = false;
        return FrameRunStatus::Faulted;
    }

    runtime_.set_joypad_buttons(joypad_pressed);
    const std::uint64_t start_cycles = runtime_.cpu.cycles;
    const std::uint64_t cycle_budget = kCpuCyclesPerFrame * (runtime_.double_speed ? 2u : 1u);
    std::size_t operations = 0;

    while ((runtime_.cpu.cycles - start_cycles) < cycle_budget && operations < operation_limit) {
        ++operations;
        auto& cpu = runtime_.cpu;
        if (cpu.faulted) {
            std::ostringstream message;
            message << "CPU fault at PC 0x" << std::hex << cpu.pc;
            error = message.str();
            running_ = false;
            return FrameRunStatus::Faulted;
        }

        const auto pending = static_cast<std::uint8_t>(
            runtime_.io[0x0F] & runtime_.interrupt_enable & 0x1F);

        if (cpu.stopped) {
            // STOP wake-up is driven by a newly pressed joypad key. While
            // stopped, advance the PPU/timer clock conservatively so the
            // frontend continues to receive frames if LCD timing is active.
            runtime_.advance_cycles(4);
            if (runtime_.ppu.frame_ready()) break;
            continue;
        }

        if (cpu.halted) {
            if (!pending) {
                runtime_.advance_cycles(4);
                if (runtime_.ppu.frame_ready()) break;
                continue;
            }
            cpu.halted = false;
        }

        if (runtime_.service_interrupt()) {
            if (runtime_.ppu.frame_ready()) break;
            continue;
        }

        const auto pc = cpu.pc;
        const auto bank = pc < 0x4000 ? std::uint16_t(0) : runtime_.rom_bank;
        bool executed_native = false;
        if (native_step_ && !cpu.halt_bug) {
            const auto next = native_step_(runtime_, bank, pc);
            if (next != kNoNativeBlock) {
                cpu.pc = next;
                ++native_block_count_;
                executed_native = true;
            } else if (cpu.faulted) {
                std::ostringstream message;
                message << "Generated code rejected an instruction at bank " << bank
                        << ":0x" << std::hex << pc;
                error = message.str();
                running_ = false;
                return FrameRunStatus::Faulted;
            }
        }

        if (!executed_native) {
            if (!runtime_.execute_interpreted_instruction(error)) {
                std::ostringstream message;
                message << "SM83 fallback failed at PC 0x" << std::hex << pc
                        << ": " << error;
                error = message.str();
                running_ = false;
                return FrameRunStatus::Faulted;
            }
            ++interpreted_instruction_count_;
        }

        if (runtime_.ppu.frame_ready()) break;
    }

    if (operations >= operation_limit && !runtime_.ppu.frame_ready()) {
        std::ostringstream message;
        message << "Instruction safety limit (" << operation_limit
                << ") reached before the PPU completed a frame; PC=0x"
                << std::hex << runtime_.cpu.pc;
        error = message.str();
        running_ = false;
        return FrameRunStatus::Faulted;
    }

    if (runtime_.ppu.consume_frame_ready()) {
        ++frame_count_;
        return FrameRunStatus::FrameReady;
    }
    return FrameRunStatus::Running;
}

} // namespace oracles
