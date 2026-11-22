#include "oracles/runtime.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace oracles {
namespace {
constexpr std::uint8_t Z=0x80, N=0x40, H=0x20, C=0x10;
std::uint16_t pair(std::uint8_t hi, std::uint8_t lo) { return static_cast<std::uint16_t>((std::uint16_t(hi)<<8)|lo); }
bool timer_signal(std::uint16_t divider, std::uint8_t tac) {
    if (!(tac & 0x04)) return false;
    constexpr unsigned bits[4] = {9, 3, 5, 7};
    return ((divider >> bits[tac & 3]) & 1u) != 0;
}
}

bool Runtime::load_rom(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if(!file) { error="Unable to open ROM: "+path.string(); return false; }
    rom.assign(std::istreambuf_iterator<char>(file),{});
    if(rom.size()<0x150 || (rom.size()%0x4000)!=0) { error="Invalid Game Boy ROM size or cartridge header."; rom.clear(); return false; }
    cpu=CpuState{};
    vram.fill(0); wram.fill(0); eram.fill(0); oam.fill(0); io.fill(0); hram.fill(0);
    interrupt_enable=0; rom_bank=1; ram_bank=0; wram_bank=1; vram_bank=0;
    ram_enabled=false; joypad_pressed=0; double_speed=false;
    constexpr std::size_t ram_sizes[6]={0,2*1024,8*1024,32*1024,128*1024,64*1024};
    eram_size_=rom.size()>0x149 && rom[0x149]<6 ? ram_sizes[rom[0x149]] : 0;
    divider_counter_=0; timer_reload_delay_=0; ppu_cycle_remainder_=0;
    serial_cycle_counter_=0; serial_bits_remaining_=0;
    pending_stall_cycles_=0;
    apu_frame_seq_cycles_=0; apu_frame_seq_step_=0;
    apu_length_counters_.fill(0); apu_active_channels_=0; apu_powered_=true;
    hdma_source_=0; hdma_destination_=0x8000; hdma_blocks_remaining_=0;
    hdma_vram_bank_=0; hdma_active_=false;
    io[0x55]=0xFF;
    io[0]=0xCF;
    ppu.reset(*this);

    // Start at the cartridge entry point with the register/I/O state left by
    // the retail CGB boot ROM. The copyrighted boot ROM is neither embedded
    // nor required; the cartridge itself is still supplied by the user.
    if (ppu.cgb_mode()) {
        cpu.a=0x11; cpu.f=0x80;
        cpu.b=0x00; cpu.c=0x00;
        cpu.d=0xFF; cpu.e=0x56;
        cpu.h=0x00; cpu.l=0x0D;
        io[0x4D]=0x7E;
    } else {
        cpu.a=0x01; cpu.f=0xB0;
        cpu.b=0x00; cpu.c=0x13;
        cpu.d=0x00; cpu.e=0xD8;
        cpu.h=0x01; cpu.l=0x4D;
        io[0x4D]=0xFF;
    }
    cpu.sp=0xFFFE; cpu.pc=0x0100;
    io[0x0F]=0xE1;
    io[0x10]=0x80; io[0x11]=0xBF; io[0x12]=0xF3; io[0x14]=0xBF;
    io[0x16]=0x3F; io[0x17]=0x00; io[0x19]=0xBF;
    io[0x1A]=0x7F; io[0x1B]=0xFF; io[0x1C]=0x9F; io[0x1E]=0xBF;
    io[0x20]=0xFF; io[0x21]=0x00; io[0x22]=0x00; io[0x23]=0xBF;
    io[0x24]=0x77; io[0x25]=0xF3; io[0x26]=ppu.cgb_mode()?0xF0:0xF1;
    io[0x47]=0xFC; io[0x48]=0xFF; io[0x49]=0xFF;
    io[0x4A]=0x00; io[0x4B]=0x00;
    return true;
}

std::uint8_t Runtime::joypad_low_nibble() const noexcept {
    const auto select = static_cast<std::uint8_t>(io[0] & 0x30u);
    std::uint8_t low = 0x0Fu;

    // P1 bit 5 (P15) selects A/B/Select/Start when cleared.
    if ((select & 0x20u) == 0) {
        if (joypad_pressed & 0x01u) low = static_cast<std::uint8_t>(low & ~0x01u); // A
        if (joypad_pressed & 0x02u) low = static_cast<std::uint8_t>(low & ~0x02u); // B
        if (joypad_pressed & 0x04u) low = static_cast<std::uint8_t>(low & ~0x04u); // Select
        if (joypad_pressed & 0x08u) low = static_cast<std::uint8_t>(low & ~0x08u); // Start
    }

    // P1 bit 4 (P14) selects Right/Left/Up/Down when cleared.
    if ((select & 0x10u) == 0) {
        if (joypad_pressed & 0x10u) low = static_cast<std::uint8_t>(low & ~0x01u); // Right
        if (joypad_pressed & 0x20u) low = static_cast<std::uint8_t>(low & ~0x02u); // Left
        if (joypad_pressed & 0x40u) low = static_cast<std::uint8_t>(low & ~0x04u); // Up
        if (joypad_pressed & 0x80u) low = static_cast<std::uint8_t>(low & ~0x08u); // Down
    }
    return low;
}

void Runtime::update_joypad_interrupt_on_falling_edge(std::uint8_t previous_low) noexcept {
    const auto current_low = joypad_low_nibble();
    // JOYP requests INT 60 when any selected P1 input line falls from high
    // to low. Merely pressing a button in the unselected row is not an edge.
    if ((previous_low & static_cast<std::uint8_t>(~current_low) & 0x0Fu) != 0) {
        io[0x0F] = static_cast<std::uint8_t>(io[0x0F] | 0x10u);
        cpu.stopped = false;
    }
}

void Runtime::set_joypad_buttons(std::uint8_t pressed_mask) {
    const auto previous_low = joypad_low_nibble();
    joypad_pressed = pressed_mask;
    update_joypad_interrupt_on_falling_edge(previous_low);
}

std::uint8_t Runtime::read8(std::uint16_t a) const {
    if(a<0x4000) return a<rom.size()?rom[a]:0xFF;
    if(a<0x8000) {
        const std::size_t bank_count=rom.size()/0x4000;
        const std::size_t selected_bank=bank_count ? (std::size_t(rom_bank)%bank_count) : 0;
        const std::size_t offset=selected_bank*0x4000+(a-0x4000);
        return offset<rom.size()?rom[offset]:0xFF;
    }
    if(a<0xA000) return ppu.cpu_can_access_vram()?vram[(std::size_t(vram_bank&1)*0x2000)+(a-0x8000)]:0xFF;
    if(a<0xC000) {
        if(!ram_enabled || eram_size_==0) return 0xFF;
        const std::size_t bank_offset=eram_size_<=0x2000 ? 0 : std::size_t(ram_bank&0x0F)*0x2000;
        const std::size_t address_offset=eram_size_<=0x2000 ? (a-0xA000)%eram_size_ : (a-0xA000);
        const std::size_t offset=bank_offset+address_offset;
        return offset<eram_size_?eram[offset]:0xFF;
    }
    if(a<0xD000) return wram[a-0xC000];
    if(a<0xE000) return wram[std::size_t(wram_bank&7)*0x1000+(a-0xD000)];
    if(a<0xF000) return wram[a-0xE000];
    if(a<0xFE00) return wram[std::size_t(wram_bank&7)*0x1000+(a-0xF000)];
    if(a<0xFEA0) return ppu.cpu_can_access_oam()?oam[a-0xFE00]:0xFF;
    if(a<0xFF00) return 0xFF;
    if(a<0xFF80) {
        if (a==0xFF04) return static_cast<std::uint8_t>(divider_counter_ >> 8);
        if (a==0xFF05) return io[0x05];
        if (a==0xFF06) return io[0x06];
        if (a==0xFF07) return static_cast<std::uint8_t>(0xF8 | (io[0x07] & 0x07));
        if (a==0xFF02) return static_cast<std::uint8_t>((ppu.cgb_mode() ? 0x7C : 0x7E) | (io[0x02] & (ppu.cgb_mode() ? 0x83 : 0x81)));
        if (a==0xFF4D) return ppu.cgb_mode()
            ? static_cast<std::uint8_t>(0x7E | (double_speed ? 0x80 : 0) | (io[0x4D] & 1)) : 0xFF;
        std::uint8_t ppu_value{};
        if(ppu.read_register(*this,a,ppu_value)) return ppu_value;
        if (a == 0xFF26) {
            return static_cast<std::uint8_t>(0x70u | (apu_powered_ ? 0x80u : 0u) | apu_active_channels_);
        }
        if (a >= 0xFF51 && a <= 0xFF54) return 0xFF;
        if (a == 0xFF55) return ppu.cgb_mode() ? io[0x55] : 0xFF;
        if(a==0xFF00) {
            const auto select = static_cast<std::uint8_t>(io[0] & 0x30u);
            return static_cast<std::uint8_t>(0xC0u | select | joypad_low_nibble());
        }
        if(a==0xFF0F) return io[0x0F] | 0xE0;
        return io[a-0xFF00];
    }
    if(a<0xFFFF) return hram[a-0xFF80];
    return interrupt_enable;
}

void Runtime::write8(std::uint16_t a, std::uint8_t v) {
    if(a<0x2000) { ram_enabled=(v&0x0F)==0x0A; return; }
    if(a<0x3000) { rom_bank=static_cast<std::uint16_t>((rom_bank&0x100)|v); return; }
    if(a<0x4000) { rom_bank=static_cast<std::uint16_t>((rom_bank&0xFF)|((v&1)<<8)); return; }
    if(a<0x6000) { ram_bank=v&0x0F; return; }
    if(a<0x8000) return;
    if(a<0xA000) { if(ppu.cpu_can_access_vram()) vram[(std::size_t(vram_bank&1)*0x2000)+(a-0x8000)]=v; return; }
    if(a<0xC000) {
        if(!ram_enabled || eram_size_==0) return;
        const std::size_t bank_offset=eram_size_<=0x2000 ? 0 : std::size_t(ram_bank&0x0F)*0x2000;
        const std::size_t address_offset=eram_size_<=0x2000 ? (a-0xA000)%eram_size_ : (a-0xA000);
        const std::size_t offset=bank_offset+address_offset;
        if(offset<eram_size_) eram[offset]=v;
        return;
    }
    if(a<0xD000) { wram[a-0xC000]=v; return; }
    if(a<0xE000) { wram[std::size_t(wram_bank&7)*0x1000+(a-0xD000)]=v; return; }
    if(a<0xF000) { wram[a-0xE000]=v; return; }
    if(a<0xFE00) { wram[std::size_t(wram_bank&7)*0x1000+(a-0xF000)]=v; return; }
    if(a<0xFEA0) { if(ppu.cpu_can_access_oam()) oam[a-0xFE00]=v; return; }
    if(a<0xFF00) return;
    if(a<0xFF80) {
        if (a==0xFF04) {
            const bool old_signal=timer_signal(divider_counter_,io[0x07]);
            divider_counter_=0;
            if (old_signal && !timer_signal(divider_counter_,io[0x07])) timer_increment();
            return;
        }
        if (a==0xFF05) { io[0x05]=v; timer_reload_delay_=0; return; }
        if (a==0xFF06) { io[0x06]=v; return; }
        if (a==0xFF07) {
            const bool old_signal=timer_signal(divider_counter_,io[0x07]);
            io[0x07]=v & 0x07;
            if (old_signal && !timer_signal(divider_counter_,io[0x07])) timer_increment();
            return;
        }
        if (a==0xFF00) {
            const auto previous_low = joypad_low_nibble();
            // Only P15/P14 are writable; bits 3..0 are active-low inputs.
            io[0] = static_cast<std::uint8_t>(v & 0x30u);
            update_joypad_interrupt_on_falling_edge(previous_low);
            return;
        }
        if (a==0xFF01) { io[0x01]=v; return; }
        if (a==0xFF02) {
            io[0x02]=static_cast<std::uint8_t>(v & (ppu.cgb_mode() ? 0x83 : 0x81));
            serial_cycle_counter_=0;
            serial_bits_remaining_=(io[0x02] & 0x80) ? 8 : 0;
            return;
        }
        if (a==0xFF4D) { if (ppu.cgb_mode()) io[0x4D]=v & 1; return; }
        if(ppu.write_register(*this,a,v)) return;
        if (a >= 0xFF10 && a <= 0xFF26) {
            write_apu_register(a, v);
            return;
        }
        if (a >= 0xFF51 && a <= 0xFF54) {
            if (!ppu.cgb_mode() || hdma_active_) return;
            switch (a) {
            case 0xFF51: io[0x51] = v; break;
            case 0xFF52: io[0x52] = static_cast<std::uint8_t>(v & 0xF0u); break;
            case 0xFF53: io[0x53] = static_cast<std::uint8_t>(v & 0x1Fu); break;
            case 0xFF54: io[0x54] = static_cast<std::uint8_t>(v & 0xF0u); break;
            }
            return;
        }
        if (a == 0xFF55) {
            start_vram_dma(v);
            return;
        }
        const auto index=static_cast<std::uint8_t>(a-0xFF00);
        io[index]=v;
        if(a==0xFF70) wram_bank=(v&7)?(v&7):1;
        if(a==0xFF0F) io[index]=v&0x1F;
        if(a==0xFF46) {
            const std::uint16_t source=static_cast<std::uint16_t>(v)<<8;
            for(unsigned i=0;i<oam.size();++i) {
                std::uint8_t value=0xFF;
                if(source+i<0xFE00 || source+i>=0xFEA0) value=read8(static_cast<std::uint16_t>(source+i));
                oam[i]=value;
            }
        }
        return;
    }
    if(a<0xFFFF) { hram[a-0xFF80]=v; return; }
    interrupt_enable=v;
}
void Runtime::timer_increment() {
    if (timer_reload_delay_) return;
    if (io[0x05] == 0xFF) {
        io[0x05] = 0x00;
        timer_reload_delay_ = 4;
    } else {
        ++io[0x05];
    }
}

void Runtime::serial_tick() {
    // With no Link Cable attached, the serial input line is pulled high.
    // Internal-clock transfers therefore shift in eight 1 bits, completing
    // with SB=$FF and requesting INT $58. External-clock transfers wait for
    // a peer to provide clock edges and must remain pending indefinitely.
    if (!serial_bits_remaining_ || !(io[0x02] & 0x80) || !(io[0x02] & 0x01)) return;
    const std::uint16_t cycles_per_bit = (ppu.cgb_mode() && (io[0x02] & 0x02))
        ? static_cast<std::uint16_t>(double_speed ? 8u : 16u) : 512u;
    if (++serial_cycle_counter_ < cycles_per_bit) return;
    serial_cycle_counter_ = 0;
    io[0x01] = static_cast<std::uint8_t>((io[0x01] << 1) | 0x01);
    --serial_bits_remaining_;
    if (!serial_bits_remaining_) {
        io[0x01] = 0xFF;
        io[0x02] = static_cast<std::uint8_t>(io[0x02] & ~0x80u);
        io[0x0F] = static_cast<std::uint8_t>(io[0x0F] | 0x08);
    }
}

void Runtime::advance_cycles(std::uint32_t t_cycles) {
    // Advance one CPU T-cycle at a time. The PPU calls on_hblank() at the
    // precise visible-line mode transition; any HDMA stall is inserted into
    // this same timeline so the PPU, timer and serial clock keep progressing
    // while the CPU is suspended for the 16-byte transfer.
    std::uint64_t remaining = std::uint64_t(t_cycles) + pending_stall_cycles_;
    pending_stall_cycles_ = 0;
    while (remaining) {
        --remaining;
        ++cpu.cycles;
        if (timer_reload_delay_ && --timer_reload_delay_ == 0) {
            io[0x05]=io[0x06];
            io[0x0F]=static_cast<std::uint8_t>(io[0x0F] | 0x04);
        }
        const bool old_signal=timer_signal(divider_counter_,io[0x07]);
        ++divider_counter_;
        const bool new_signal=timer_signal(divider_counter_,io[0x07]);
        if (old_signal && !new_signal) timer_increment();
        serial_tick();

        if (apu_powered_) {
            const std::uint32_t apu_period = double_speed ? 16384u : 8192u;
            if (++apu_frame_seq_cycles_ >= apu_period) {
                apu_frame_seq_cycles_ = 0;
                apu_tick();
            }
        }

        if (double_speed) {
            if (++ppu_cycle_remainder_ >= 2) {
                ppu_cycle_remainder_ = 0;
                ppu.tick(*this,1);
            }
        } else {
            ppu.tick(*this,1);
        }
        if (pending_stall_cycles_) {
            remaining += pending_stall_cycles_;
            pending_stall_cycles_ = 0;
        }
    }
    if (cpu.ime_delay) {
        --cpu.ime_delay;
        if (!cpu.ime_delay) cpu.ime=true;
    }
}

void Runtime::write_apu_register(std::uint16_t address, std::uint8_t value) {
    if (address == 0xFF26) {
        const bool enable = (value & 0x80) != 0;
        if (!enable) {
            apu_powered_ = false;
            apu_active_channels_ = 0;
            apu_length_counters_.fill(0);
            apu_frame_seq_cycles_ = 0;
            apu_frame_seq_step_ = 0;
            for (unsigned index = 0x10; index <= 0x25; ++index) io[index] = 0;
        } else if (!apu_powered_) {
            apu_powered_ = true;
            apu_active_channels_ = 0;
            apu_length_counters_.fill(0);
            apu_frame_seq_cycles_ = 0;
            apu_frame_seq_step_ = 0;
        }
        io[0x26] = apu_powered_ ? 0x80 : 0x00;
        return;
    }

    const auto index = static_cast<std::uint8_t>(address - 0xFF00);
    if (!apu_powered_) {
        // Length registers remain writable while the APU is powered down.
        if (address == 0xFF11 || address == 0xFF16) {
            io[index] = value;
            apu_length_counters_[(address == 0xFF11) ? 0 : 1] = 64u - (value & 0x3Fu);
        } else if (address == 0xFF1B) {
            io[index] = value;
            apu_length_counters_[2] = 256u - value;
        } else if (address == 0xFF20) {
            io[index] = value;
            apu_length_counters_[3] = 64u - (value & 0x3Fu);
        }
        return;
    }

    io[index] = value;
    if (address == 0xFF11) apu_length_counters_[0] = 64u - (value & 0x3Fu);
    else if (address == 0xFF16) apu_length_counters_[1] = 64u - (value & 0x3Fu);
    else if (address == 0xFF1B) apu_length_counters_[2] = 256u - value;
    else if (address == 0xFF20) apu_length_counters_[3] = 64u - (value & 0x3Fu);

    // DAC disable immediately turns off its channel. NR52 channel flags are
    // read-only status bits, not a mirror of software writes.
    constexpr std::array<std::uint16_t,4> dac_registers = {0xFF12,0xFF17,0xFF1A,0xFF21};
    for (unsigned channel = 0; channel < dac_registers.size(); ++channel) {
        if (address != dac_registers[channel]) continue;
        const bool dac_enabled = channel == 2 ? (value & 0x80u) != 0 : (value & 0xF8u) != 0;
        if (!dac_enabled) apu_active_channels_ = static_cast<std::uint8_t>(apu_active_channels_ & ~(1u << channel));
        break;
    }

    constexpr std::array<std::uint16_t,4> trigger_registers = {0xFF14,0xFF19,0xFF1E,0xFF23};
    constexpr std::array<std::uint16_t,4> dac_indices = {0x12,0x17,0x1A,0x21};
    for (unsigned channel = 0; channel < trigger_registers.size(); ++channel) {
        if (address != trigger_registers[channel]) continue;
        if (value & 0x80) {
            const bool dac_enabled = channel == 2 ? (io[dac_indices[channel]] & 0x80u) != 0
                                                  : (io[dac_indices[channel]] & 0xF8u) != 0;
            if (dac_enabled) {
                const std::uint16_t maximum = channel == 2 ? 256u : 64u;
                if (apu_length_counters_[channel] == 0) apu_length_counters_[channel] = maximum;
                apu_active_channels_ = static_cast<std::uint8_t>(apu_active_channels_ | (1u << channel));
            }
        }
        break;
    }
}

void Runtime::apu_tick() {
    // The 512 Hz frame sequencer clocks length counters on steps 0, 2, 4, 6.
    if ((apu_frame_seq_step_ & 1u) == 0) {
        constexpr std::array<std::uint16_t,4> length_enable_registers = {0x14,0x19,0x1E,0x23};
        for (unsigned channel = 0; channel < length_enable_registers.size(); ++channel) {
            if (!(io[length_enable_registers[channel]] & 0x40)) continue;
            if (apu_length_counters_[channel] && --apu_length_counters_[channel] == 0) {
                apu_active_channels_ = static_cast<std::uint8_t>(apu_active_channels_ & ~(1u << channel));
            }
        }
    }
    apu_frame_seq_step_ = static_cast<std::uint8_t>((apu_frame_seq_step_ + 1u) & 7u);
}

void Runtime::transfer_vram_dma_block() {
    for (unsigned byte = 0; byte < 16; ++byte) {
        const auto source = static_cast<std::uint16_t>(hdma_source_ + byte);
        const auto destination = static_cast<std::uint16_t>(0x8000u |
            ((hdma_destination_ - 0x8000u + byte) & 0x1FFFu));
        const auto value = read8(source);
        const std::size_t target = std::size_t(hdma_vram_bank_ & 1u) * 0x2000u +
                                   (destination - 0x8000u);
        vram[target] = value;
    }
    hdma_source_ = static_cast<std::uint16_t>(hdma_source_ + 0x10u);
    hdma_destination_ = static_cast<std::uint16_t>(0x8000u |
        ((hdma_destination_ - 0x8000u + 0x10u) & 0x1FF0u));
    io[0x51] = static_cast<std::uint8_t>(hdma_source_ >> 8);
    io[0x52] = static_cast<std::uint8_t>(hdma_source_ & 0xF0u);
    io[0x53] = static_cast<std::uint8_t>((hdma_destination_ >> 8) & 0x1Fu);
    io[0x54] = static_cast<std::uint8_t>(hdma_destination_ & 0xF0u);
}

void Runtime::start_vram_dma(std::uint8_t value) {
    if (!ppu.cgb_mode()) return;
    if (hdma_active_) {
        if (!(value & 0x80)) {
            hdma_active_ = false;
            io[0x55] = static_cast<std::uint8_t>(0x80u | ((hdma_blocks_remaining_ - 1u) & 0x7Fu));
        }
        return;
    }

    hdma_source_ = static_cast<std::uint16_t>((std::uint16_t(io[0x51]) << 8) | (io[0x52] & 0xF0u));
    hdma_destination_ = static_cast<std::uint16_t>(0x8000u |
        ((std::uint16_t(io[0x53] & 0x1Fu) << 8) | (io[0x54] & 0xF0u)));
    hdma_blocks_remaining_ = static_cast<std::uint8_t>((value & 0x7Fu) + 1u);
    hdma_vram_bank_ = vram_bank & 1u;

    const bool hblank_mode = (value & 0x80u) && (io[0x40] & 0x80u);
    if (hblank_mode) {
        hdma_active_ = true;
        io[0x55] = static_cast<std::uint8_t>(hdma_blocks_remaining_ - 1u);
        return;
    }

    hdma_active_ = false;
    while (hdma_blocks_remaining_) {
        transfer_vram_dma_block();
        --hdma_blocks_remaining_;
        pending_stall_cycles_ += double_speed ? 64u : 32u;
    }
    io[0x55] = 0xFF;
}

void Runtime::on_hblank() {
    // CGB HBlank DMA pauses while the CPU is halted; the outstanding block
    // is resumed at a later visible HBlank after execution wakes up.
    if (cpu.halted || cpu.stopped || !hdma_active_ || hdma_blocks_remaining_ == 0) return;
    transfer_vram_dma_block();
    --hdma_blocks_remaining_;
    pending_stall_cycles_ += double_speed ? 64u : 32u;
    if (hdma_blocks_remaining_ == 0) {
        hdma_active_ = false;
        io[0x55] = 0xFF;
    } else {
        io[0x55] = static_cast<std::uint8_t>(hdma_blocks_remaining_ - 1u);
    }
}

bool Runtime::handle_stop_speed_switch() noexcept {
    if (!ppu.cgb_mode() || !(io[0x4D] & 1)) return false;
    double_speed=!double_speed;
    io[0x4D]=static_cast<std::uint8_t>(0x7E | (double_speed ? 0x80 : 0));
    divider_counter_=0;
    ppu_cycle_remainder_=0;
    cpu.stopped=false;
    return true;
}

bool Runtime::should_yield_native_block() const noexcept {
    const auto pending = static_cast<std::uint8_t>(io[0x0F] & interrupt_enable & 0x1F);
    return cpu.faulted || cpu.halted || cpu.stopped || ppu.frame_ready() || (cpu.ime && pending != 0);
}

bool Runtime::service_interrupt() {
    const std::uint8_t pending=static_cast<std::uint8_t>(io[0x0F] & interrupt_enable & 0x1F);
    if (!pending) return false;
    cpu.halted=false;
    if (!cpu.ime) return false;
    unsigned bit=0;
    while (bit<5 && !(pending & (1u<<bit))) ++bit;
    if (bit>=5) return false;
    cpu.ime=false;
    cpu.ime_delay=0;
    io[0x0F]=static_cast<std::uint8_t>(io[0x0F] & ~(1u<<bit));
    push(cpu.pc);
    cpu.pc=static_cast<std::uint16_t>(0x0040 + bit*8);
    advance_cycles(20);
    return true;
}

bool Runtime::execute_interpreted_instruction(std::string& error) {
    error.clear();
    if (cpu.faulted) { error="CPU is already faulted"; return false; }
    auto& c=cpu;
    const std::uint16_t instruction_pc=c.pc;
    const std::uint8_t op=read8(instruction_pc);
    std::uint16_t cursor=instruction_pc;
    if (c.halt_bug) c.halt_bug=false; else ++cursor;
    auto fetch8=[&]() { const auto value=read8(cursor); ++cursor; return value; };
    auto fetch16=[&]() { const auto lo=fetch8(); const auto hi=fetch8(); return static_cast<std::uint16_t>(lo | (std::uint16_t(hi)<<8)); };
    unsigned cycles=4;
    std::uint16_t next_pc=cursor;

    if (op==0xCB) {
        const std::uint8_t cb=fetch8();
        const unsigned x=cb>>6, y=(cb>>3)&7, z=cb&7;
        if (x==0) cb_rotate(y,z);
        else if (x==1) cb_bit(y,z);
        else if (x==2) cb_res(y,z);
        else cb_set(y,z);
        cycles=(z==6) ? (x==1 ? 12u : 16u) : 8u;
        next_pc=cursor;
    } else {
        const unsigned x=op>>6, y=(op>>3)&7, z=op&7, p=y>>1, q=y&1;
        if (x==1) {
            if (op==0x76) {
                const auto pending=static_cast<std::uint8_t>(io[0x0F] & interrupt_enable & 0x1F);
                if (!c.ime && pending) c.halt_bug=true;
                else c.halted=true;
            } else set_r8(y,get_r8(z));
            cycles=(op==0x76 || (y!=6 && z!=6)) ? 4u : 8u;
        } else if (x==2) {
            alu(y,get_r8(z));
            cycles=z==6 ? 8u : 4u;
        } else if (x==0) {
            switch (z) {
            case 0:
                if (y==0) cycles=4;
                else if (y==1) { write16(fetch16(),c.sp); next_pc=cursor; cycles=20; }
                else if (y==2) { (void)fetch8(); next_pc=cursor; if (!handle_stop_speed_switch()) c.stopped=true; cycles=4; }
                else {
                    const auto displacement=static_cast<std::int8_t>(fetch8());
                    const auto target=static_cast<std::uint16_t>(cursor+displacement);
                    if (y==3 || condition(y-4)) { next_pc=target; cycles=12; }
                    else { next_pc=cursor; cycles=8; }
                }
                break;
            case 1:
                if (!q) { set_r16(p,fetch16()); next_pc=cursor; cycles=12; }
                else { add_hl(get_r16(p)); cycles=8; }
                break;
            case 2: {
                const std::uint16_t address=(p==0)?get_r16(0):(p==1)?get_r16(1):get_hl();
                if (!q) write8(address,c.a); else c.a=read8(address);
                if (p==2) set_hl(static_cast<std::uint16_t>(get_hl()+1));
                if (p==3) set_hl(static_cast<std::uint16_t>(get_hl()-1));
                cycles=8;
                break;
            }
            case 3:
                set_r16(p,static_cast<std::uint16_t>(get_r16(p)+(q?-1:1)));
                cycles=8;
                break;
            case 4: inc8(y); cycles=y==6?12u:4u; break;
            case 5: dec8(y); cycles=y==6?12u:4u; break;
            case 6: set_r8(y,fetch8()); next_pc=cursor; cycles=y==6?12u:8u; break;
            case 7:
                if (y<=3) rotate_a(y);
                else if (y==4) decimal_adjust();
                else if (y==5) complement_a();
                else if (y==6) set_carry(true);
                else complement_carry();
                break;
            }
        } else {
            switch (z) {
            case 0:
                if (y<4) {
                    if (condition(y)) { next_pc=pop(); cycles=20; }
                    else { next_pc=cursor; cycles=8; }
                } else if (y==4) { write8(static_cast<std::uint16_t>(0xFF00|fetch8()),c.a); next_pc=cursor; cycles=12; }
                else if (y==5) { add_sp_signed(static_cast<std::int8_t>(fetch8()),false); next_pc=cursor; cycles=16; }
                else if (y==6) { c.a=read8(static_cast<std::uint16_t>(0xFF00|fetch8())); next_pc=cursor; cycles=12; }
                else { add_sp_signed(static_cast<std::int8_t>(fetch8()),true); next_pc=cursor; cycles=12; }
                break;
            case 1:
                if (!q) {
                    const auto value=pop();
                    if (p==3) { c.a=static_cast<std::uint8_t>(value>>8); c.f=static_cast<std::uint8_t>(value)&0xF0; }
                    else set_r16(p,value);
                    cycles=12;
                } else if (p==0 || p==1) {
                    next_pc=pop(); cycles=16;
                    if (p==1) { c.ime=true; c.ime_delay=0; }
                } else if (p==2) { next_pc=get_hl(); cycles=4; }
                else { c.sp=get_hl(); cycles=8; }
                break;
            case 2:
                if (y<4) {
                    const auto target=fetch16();
                    next_pc=condition(y)?target:cursor; cycles=condition(y)?16u:12u;
                } else if (y==4) { write8(static_cast<std::uint16_t>(0xFF00|c.c),c.a); cycles=8; }
                else if (y==5) { write8(fetch16(),c.a); next_pc=cursor; cycles=16; }
                else if (y==6) { c.a=read8(static_cast<std::uint16_t>(0xFF00|c.c)); cycles=8; }
                else { c.a=read8(fetch16()); next_pc=cursor; cycles=16; }
                break;
            case 3:
                if (y==0) { next_pc=fetch16(); cycles=16; }
                else if (y==6) { c.ime=false; c.ime_delay=0; }
                else if (y==7) { if (!c.ime) c.ime_delay=2; }
                else { c.faulted=true; error="Illegal SM83 opcode at PC 0x"+std::to_string(instruction_pc); return false; }
                break;
            case 4:
                if (y<4) {
                    const auto target=fetch16();
                    if (condition(y)) { push(cursor); next_pc=target; cycles=24; }
                    else { next_pc=cursor; cycles=12; }
                } else { c.faulted=true; error="Illegal SM83 CALL opcode"; return false; }
                break;
            case 5:
                if (!q) {
                    const auto value=(p==3)?static_cast<std::uint16_t>((std::uint16_t(c.a)<<8)|(c.f&0xF0)):get_r16(p);
                    push(value); cycles=16;
                } else if (p==0) { const auto target=fetch16(); push(cursor); next_pc=target; cycles=24; }
                else { c.faulted=true; error="Illegal SM83 CALL opcode"; return false; }
                break;
            case 6: alu(y,fetch8()); next_pc=cursor; cycles=8; break;
            case 7: push(cursor); next_pc=static_cast<std::uint16_t>(y*8); cycles=16; break;
            }
        }
    }

    c.pc=next_pc;
    advance_cycles(cycles);
    if (c.faulted) { error="CPU entered a fault state"; return false; }
    return true;
}

CycleAdvance Runtime::defer_cycles(std::uint32_t t_cycles) noexcept { return CycleAdvance(*this,t_cycles); }
CycleAdvance::CycleAdvance(Runtime& runtime, std::uint32_t t_cycles) noexcept
    : runtime_(&runtime), t_cycles_(t_cycles) {}
CycleAdvance::~CycleAdvance() { if (runtime_) runtime_->advance_cycles(t_cycles_); }
std::uint16_t Runtime::read16(std::uint16_t a) const { return static_cast<std::uint16_t>(read8(a)|(std::uint16_t(read8(static_cast<std::uint16_t>(a+1)))<<8)); }
void Runtime::write16(std::uint16_t a,std::uint16_t v) { write8(a,static_cast<std::uint8_t>(v)); write8(static_cast<std::uint16_t>(a+1),static_cast<std::uint8_t>(v>>8)); }
std::uint8_t Runtime::get_r8(unsigned i) const {
    switch(i&7){case 0:return cpu.b;case 1:return cpu.c;case 2:return cpu.d;case 3:return cpu.e;case 4:return cpu.h;case 5:return cpu.l;case 6:return read8(pair(cpu.h,cpu.l));default:return cpu.a;}
}
void Runtime::set_r8(unsigned i,std::uint8_t v) {
    switch(i&7){case 0:cpu.b=v;break;case 1:cpu.c=v;break;case 2:cpu.d=v;break;case 3:cpu.e=v;break;case 4:cpu.h=v;break;case 5:cpu.l=v;break;case 6:write8(pair(cpu.h,cpu.l),v);break;default:cpu.a=v;break;}
}
std::uint16_t Runtime::get_r16(unsigned i) const { switch(i&3){case 0:return pair(cpu.b,cpu.c);case 1:return pair(cpu.d,cpu.e);case 2:return pair(cpu.h,cpu.l);default:return cpu.sp;} }
void Runtime::set_r16(unsigned i,std::uint16_t v) { switch(i&3){case 0:cpu.b=static_cast<std::uint8_t>(v>>8);cpu.c=static_cast<std::uint8_t>(v&0xFF);break;case 1:cpu.d=static_cast<std::uint8_t>(v>>8);cpu.e=static_cast<std::uint8_t>(v&0xFF);break;case 2:set_hl(v);break;default:cpu.sp=v;break;} }
std::uint16_t Runtime::get_hl() const { return pair(cpu.h,cpu.l); }
void Runtime::set_hl(std::uint16_t v) { cpu.h=static_cast<std::uint8_t>(v>>8);cpu.l=static_cast<std::uint8_t>(v&0xFF); }
bool Runtime::condition(unsigned i) const { switch(i&3){case 0:return !(cpu.f&Z);case 1:return cpu.f&Z;case 2:return !(cpu.f&C);default:return cpu.f&C;} }
void Runtime::alu(unsigned op,std::uint8_t v) {
    const unsigned a=cpu.a, carry=(cpu.f&C)?1:0;
    unsigned result=0;
    switch(op&7){
    case 0: result=a+v; cpu.f=0; if(((a&15)+(v&15))>15)cpu.f|=H; if(result>255)cpu.f|=C; cpu.a=result&255; if(!cpu.a)cpu.f|=Z; break;
    case 1: result=a+v+carry; cpu.f=0; if(((a&15)+(v&15)+carry)>15)cpu.f|=H; if(result>255)cpu.f|=C; cpu.a=result&255; if(!cpu.a)cpu.f|=Z; break;
    case 2: result=a-v; cpu.f=N; if((a&15)<(v&15))cpu.f|=H; if(a<v)cpu.f|=C; cpu.a=result&255; if(!cpu.a)cpu.f|=Z; break;
    case 3: result=a-v-carry; cpu.f=N; if((a&15)<((v&15)+carry))cpu.f|=H; if(a<(unsigned(v)+carry))cpu.f|=C; cpu.a=result&255; if(!cpu.a)cpu.f|=Z; break;
    case 4: cpu.a&=v; cpu.f=H; if(!cpu.a)cpu.f|=Z; break;
    case 5: cpu.a^=v; cpu.f=0; if(!cpu.a)cpu.f|=Z; break;
    case 6: cpu.a|=v; cpu.f=0; if(!cpu.a)cpu.f|=Z; break;
    case 7: result=a-v; cpu.f=N; if((a&15)<(v&15))cpu.f|=H; if(a<v)cpu.f|=C; if((result&255)==0)cpu.f|=Z; break;
    }
}
void Runtime::inc8(unsigned i) { const auto old=get_r8(i); const auto v=static_cast<std::uint8_t>(old+1); cpu.f=(cpu.f&C)|(v==0?Z:0)|(((old&15)==15)?H:0); set_r8(i,v); }
void Runtime::dec8(unsigned i) { const auto old=get_r8(i); const auto v=static_cast<std::uint8_t>(old-1); cpu.f=(cpu.f&C)|N|(v==0?Z:0)|((old&15)==0?H:0); set_r8(i,v); }
void Runtime::add_hl(std::uint16_t v) { const auto hl=get_hl(); const unsigned result=unsigned(hl)+v; cpu.f=(cpu.f&Z)|(((hl&0xFFF)+(v&0xFFF)>0xFFF)?H:0)|(result>0xFFFF?C:0); set_hl(static_cast<std::uint16_t>(result)); }
void Runtime::rotate_a(unsigned op) {
    const auto a=cpu.a; const bool oldc=(cpu.f&C)!=0; bool out=false;
    switch(op&3){case 0:out=(a&0x80)!=0;cpu.a=static_cast<std::uint8_t>((a<<1)|(out?1:0));break;case 1:out=(a&1)!=0;cpu.a=static_cast<std::uint8_t>((a>>1)|(out?0x80:0));break;case 2:out=(a&0x80)!=0;cpu.a=static_cast<std::uint8_t>((a<<1)|(oldc?1:0));break;case 3:out=(a&1)!=0;cpu.a=static_cast<std::uint8_t>((a>>1)|(oldc?0x80:0));break;}
    cpu.f=out?C:0;
}
void Runtime::decimal_adjust() {
    std::uint8_t correction=0; bool carry=(cpu.f&C)!=0;
    if(!(cpu.f&N)) { if((cpu.f&H)||(cpu.a&0x0F)>9)correction|=0x06; if(carry||cpu.a>0x99){correction|=0x60;carry=true;} cpu.a=static_cast<std::uint8_t>(cpu.a+correction); }
    else { if(cpu.f&H)correction|=0x06; if(carry)correction|=0x60; cpu.a=static_cast<std::uint8_t>(cpu.a-correction); }
    cpu.f=(cpu.f&N)|(cpu.a==0?Z:0)|(carry?C:0);
}
void Runtime::complement_a() { cpu.a=~cpu.a; cpu.f=(cpu.f&(Z|C))|N|H; }
void Runtime::set_carry(bool set) { cpu.f=(cpu.f&Z)|(set?C:0); }
void Runtime::complement_carry() { cpu.f=(cpu.f&Z)|((cpu.f&C)?0:C); }
void Runtime::push(std::uint16_t v) { cpu.sp=static_cast<std::uint16_t>(cpu.sp-1);write8(cpu.sp,static_cast<std::uint8_t>(v>>8));cpu.sp=static_cast<std::uint16_t>(cpu.sp-1);write8(cpu.sp,static_cast<std::uint8_t>(v)); }
std::uint16_t Runtime::pop() { const auto lo=read8(cpu.sp);cpu.sp=static_cast<std::uint16_t>(cpu.sp+1);const auto hi=read8(cpu.sp);cpu.sp=static_cast<std::uint16_t>(cpu.sp+1);return static_cast<std::uint16_t>(lo|(std::uint16_t(hi)<<8)); }
void Runtime::add_sp_signed(std::int8_t value,bool to_hl) {
    const auto sp=cpu.sp;const unsigned u=static_cast<std::uint8_t>(value);const auto result=static_cast<std::uint16_t>(sp+value);
    cpu.f=0;if(((sp&0xF)+(u&0xF))>0xF)cpu.f|=H;if(((sp&0xFF)+u)>0xFF)cpu.f|=C;
    if(to_hl)set_hl(result);else cpu.sp=result;
}
void Runtime::cb_rotate(unsigned op,unsigned reg) {
    auto v=get_r8(reg);bool carry=false;const bool oldc=(cpu.f&C)!=0;
    switch(op&7){case 0:carry=v&0x80;v=static_cast<std::uint8_t>((v<<1)|(carry?1:0));break;case 1:carry=v&1;v=static_cast<std::uint8_t>((v>>1)|(carry?0x80:0));break;case 2:carry=v&0x80;v=static_cast<std::uint8_t>((v<<1)|(oldc?1:0));break;case 3:carry=v&1;v=static_cast<std::uint8_t>((v>>1)|(oldc?0x80:0));break;case 4:carry=v&0x80;v=static_cast<std::uint8_t>(v<<1);break;case 5:carry=v&1;v=static_cast<std::uint8_t>((v>>1)|(v&0x80));break;case 6:v=static_cast<std::uint8_t>((v<<4)|(v>>4));break;case 7:carry=v&1;v>>=1;break;}
    set_r8(reg,v);cpu.f=(v==0?Z:0)|(carry?C:0);
}
void Runtime::cb_bit(unsigned bit,unsigned reg) { const auto v=get_r8(reg);cpu.f=(cpu.f&C)|H|((v&(1u<<(bit&7)))?0:Z); }
void Runtime::cb_res(unsigned bit,unsigned reg) { set_r8(reg,static_cast<std::uint8_t>(get_r8(reg)&~(1u<<(bit&7)))); }
void Runtime::cb_set(unsigned bit,unsigned reg) { set_r8(reg,static_cast<std::uint8_t>(get_r8(reg)|(1u<<(bit&7)))); }

NativeProbeResult execute_native_probe(Runtime& runtime, NativeStep step, std::size_t max_blocks) {
    NativeProbeResult result;
    if (!step) { result.diagnostic = "No generated native dispatcher was linked."; return result; }
    for (std::size_t i=0; i<max_blocks; ++i) {
        if (runtime.cpu.halted || runtime.cpu.stopped) { result.stopped_on_halt = true; break; }
        const auto pc = runtime.cpu.pc;
        const auto bank = pc < 0x4000 ? std::uint16_t(0) : runtime.rom_bank;
        const auto next = step(runtime, bank, pc);
        if (next == kNoNativeBlock) {
            result.diagnostic = "No compiled native block at bank " + std::to_string(bank) + ":" + std::to_string(pc) + ".";
            result.final_pc = pc;
            return result;
        }
        runtime.cpu.pc = next;
        ++result.blocks_executed;
        if (runtime.cpu.faulted) {
            result.diagnostic = "A generated block reported an illegal or unsupported instruction.";
            result.final_pc = runtime.cpu.pc;
            return result;
        }
    }
    result.final_pc = runtime.cpu.pc;
    if (result.diagnostic.empty()) {
        result.diagnostic = result.stopped_on_halt ? "Execution reached HALT/STOP." : "Native block probe reached its configured step limit.";
    }
    return result;
}
}
