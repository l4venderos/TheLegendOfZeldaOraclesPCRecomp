#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "oracles/ppu.hpp"

namespace oracles {
struct CpuState {
    std::uint8_t a{}, f{}, b{}, c{}, d{}, e{}, h{}, l{};
    std::uint16_t sp{0xFFFE}, pc{0x0100};
    std::uint64_t cycles{};
    bool ime{};
    bool halted{};
    bool stopped{};
    bool faulted{};
    bool halt_bug{};
    std::uint8_t ime_delay{};
};

class Runtime;

// Defers PPU/cpu-cycle advancement until an emitted instruction finishes.
// Its destructor also runs on generated early-return control-flow paths.
class CycleAdvance {
public:
    CycleAdvance(Runtime& runtime, std::uint32_t t_cycles) noexcept;
    ~CycleAdvance();
    CycleAdvance(const CycleAdvance&) = delete;
    CycleAdvance& operator=(const CycleAdvance&) = delete;
    CycleAdvance(CycleAdvance&&) = delete;
    CycleAdvance& operator=(CycleAdvance&&) = delete;
private:
    Runtime* runtime_{};
    std::uint32_t t_cycles_{};
};

class Runtime {
public:
    CpuState cpu;
    std::vector<std::uint8_t> rom;
    std::array<std::uint8_t, 0x4000> vram{};
    std::array<std::uint8_t, 0x8000> wram{};
    // MBC5 cartridges can expose up to sixteen 8 KiB external-RAM banks.
    std::array<std::uint8_t, 0x20000> eram{};
    std::array<std::uint8_t, 0xA0> oam{};
    std::array<std::uint8_t, 0x80> io{};
    std::array<std::uint8_t, 0x7F> hram{};
    std::uint8_t interrupt_enable{};
    std::uint16_t rom_bank{1};
    std::uint8_t ram_bank{};
    std::uint8_t vram_bank{};
    std::uint8_t wram_bank{1};
    std::uint8_t joypad_pressed{};
    bool ram_enabled{};
    bool double_speed{};
    Ppu ppu;

private:
    std::uint16_t divider_counter_{};
    std::uint8_t timer_reload_delay_{};
    std::uint8_t ppu_cycle_remainder_{};
    std::uint16_t serial_cycle_counter_{};
    std::uint8_t serial_bits_remaining_{};
    std::size_t eram_size_{};
    std::uint32_t pending_stall_cycles_{};
    std::uint32_t apu_frame_seq_cycles_{};
    std::uint8_t apu_frame_seq_step_{};
    std::array<std::uint16_t, 4> apu_length_counters_{};
    std::uint8_t apu_active_channels_{};
    bool apu_powered_{true};
    std::uint16_t hdma_source_{};
    std::uint16_t hdma_destination_{0x8000};
    std::uint8_t hdma_blocks_remaining_{};
    std::uint8_t hdma_vram_bank_{};
    bool hdma_active_{};
    // Return the active-low P1 input nibble after applying the currently
    // selected button/direction rows. P1 bit 5 selects buttons; bit 4 selects
    // the D-pad.
    std::uint8_t joypad_low_nibble() const noexcept;
    void update_joypad_interrupt_on_falling_edge(std::uint8_t previous_low) noexcept;
    void timer_increment();
    void serial_tick();
    void apu_tick();
    void write_apu_register(std::uint16_t address, std::uint8_t value);
    void start_vram_dma(std::uint8_t value);
    void transfer_vram_dma_block();

public:

    bool load_rom(const std::filesystem::path& path, std::string& error);
    void set_joypad_buttons(std::uint8_t pressed_mask);
    std::uint8_t read8(std::uint16_t address) const;
    void write8(std::uint16_t address, std::uint8_t value);
    std::uint16_t read16(std::uint16_t address) const;
    void write16(std::uint16_t address, std::uint16_t value);
    // CPU T-cycles are also PPU dots in normal-speed CGB mode.
    void advance_cycles(std::uint32_t t_cycles);
    bool service_interrupt();
    // Called by the PPU at the start of each visible scanline's HBlank.
    void on_hblank();
    // Generated blocks yield at instruction boundaries when the scheduler
    // must present a frame, service an enabled interrupt, or honor HALT/STOP.
    bool should_yield_native_block() const noexcept;
    bool execute_interpreted_instruction(std::string& error);
    bool handle_stop_speed_switch() noexcept;
    CycleAdvance defer_cycles(std::uint32_t t_cycles) noexcept;
    std::uint8_t get_r8(unsigned index) const;
    void set_r8(unsigned index, std::uint8_t value);
    std::uint16_t get_r16(unsigned index) const;
    void set_r16(unsigned index, std::uint16_t value);
    std::uint16_t get_hl() const;
    void set_hl(std::uint16_t value);
    bool condition(unsigned index) const;
    void alu(unsigned operation, std::uint8_t value);
    void inc8(unsigned index);
    void dec8(unsigned index);
    void add_hl(std::uint16_t value);
    void rotate_a(unsigned operation);
    void decimal_adjust();
    void complement_a();
    void set_carry(bool set);
    void complement_carry();
    void push(std::uint16_t value);
    std::uint16_t pop();
    void add_sp_signed(std::int8_t value, bool to_hl);
    void cb_rotate(unsigned operation, unsigned reg);
    void cb_bit(unsigned bit, unsigned reg);
    void cb_res(unsigned bit, unsigned reg);
    void cb_set(unsigned bit, unsigned reg);
};

using NativeStep = std::uint16_t (*)(Runtime&, std::uint16_t bank, std::uint16_t pc);
constexpr std::uint16_t kNoNativeBlock = 0xFFFF;
struct NativeProbeResult {
    std::size_t blocks_executed{};
    std::uint16_t final_pc{};
    bool stopped_on_halt{};
    std::string diagnostic;
};
NativeProbeResult execute_native_probe(Runtime& runtime, NativeStep step, std::size_t max_blocks = 12);
}
