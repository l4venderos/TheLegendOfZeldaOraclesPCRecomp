#include "oracles/runtime.hpp"
#include "oracles/game_runner.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <iostream>
#include <array>
#include <cstdint>
#include <memory>
#include <filesystem>
#include <fstream>
#include <vector>
namespace {
void set_palette(oracles::Runtime& r, std::uint16_t base, const std::array<std::uint16_t,4>& colours) {
    r.write8(base, 0x80);
    for (const auto colour : colours) {
        r.write8(static_cast<std::uint16_t>(base + 1), static_cast<std::uint8_t>(colour & 0xFF));
        r.write8(static_cast<std::uint16_t>(base + 1), static_cast<std::uint8_t>(colour >> 8));
    }
}
void setup_cgb(oracles::Runtime& r) {
    r.rom.assign(0x8000, 0);
    r.rom[0x143] = 0x80;
    r.ppu.reset(r);
}
void set_palette_at(oracles::Runtime& r, std::uint16_t base, std::uint8_t index,
                    const std::array<std::uint16_t,4>& colours) {
    r.write8(base, static_cast<std::uint8_t>(0x80 | (index & 0x3F)));
    for (const auto colour : colours) {
        r.write8(static_cast<std::uint16_t>(base + 1), static_cast<std::uint8_t>(colour & 0xFF));
        r.write8(static_cast<std::uint16_t>(base + 1), static_cast<std::uint8_t>(colour >> 8));
    }
}
}

// Runtime contains a 160x144 RGBA framebuffer plus the Game Boy memory map.
// Keep the independent test machines in one heap allocation: MSVC executables
// reserve a 1 MiB stack by default, and placing every Runtime local in main()
// makes the function's stack frame exceed that limit before tests can run.
struct RuntimeFixtures {
    oracles::Runtime r;
    oracles::Runtime video;
    oracles::Runtime banks;
    oracles::Runtime bus_lock;
    oracles::Runtime objects;
    oracles::Runtime priority;
    oracles::Runtime attributes;
    oracles::Runtime window;
    oracles::Runtime dmg;
    oracles::Runtime stat;
    oracles::Runtime penalties;
    oracles::Runtime clocked;
    oracles::Runtime interpreted;
    oracles::Runtime timer;
    oracles::Runtime serial;
    oracles::Runtime interrupt;
    oracles::Runtime apu;
    oracles::Runtime gdma;
    oracles::Runtime hdma;
    oracles::Runtime hdma_halt;
    oracles::GameRunner live_game;
};

int main() {
    const auto fixtures = std::make_unique<RuntimeFixtures>();
    auto& r = fixtures->r;
    r.rom.resize(3 * 0x4000);
    r.rom[0x4000] = 1;
    r.rom[2 * 0x4000] = 2;
    r.rom_bank = 2;
    assert(r.read8(0x4000) == 2);
    r.write8(0x2000, 1);
    assert(r.read8(0x4000) == 1);
    r.write8(0x0000, 0x0A);
    assert(r.ram_enabled);
    r.cpu.a = 0xFE;
    r.cpu.f = 0;
    r.alu(0, 2);
    assert(r.cpu.a == 0);
    assert((r.cpu.f & 0x80) != 0);
    assert((r.cpu.f & 0x10) != 0);
    r.set_hl(0xC123);

    // P1/JOYP row selection is active-low: bit 5 selects action buttons,
    // while bit 4 selects directions. Also verify unselected-row isolation.
    r.write8(0xFF0F, 0);
    r.write8(0xFF00, 0x10); // P15=0: select A/B/Select/Start
    r.set_joypad_buttons(0x01); // A pressed
    assert((r.read8(0xFF00) & 0x0Fu) == 0x0Eu);
    assert((r.read8(0xFF0F) & 0x10u) != 0); // selected P1 line fell high->low

    r.write8(0xFF0F, 0);
    r.set_joypad_buttons(0x10); // Right pressed, but only action row selected
    assert((r.read8(0xFF00) & 0x0Fu) == 0x0Fu);
    assert((r.read8(0xFF0F) & 0x10u) == 0); // unselected row causes no P1 interrupt

    r.write8(0xFF00, 0x20); // P14=0: select D-pad
    assert((r.read8(0xFF00) & 0x0Fu) == 0x0Eu); // held Right becomes visible
    assert((r.read8(0xFF0F) & 0x10u) != 0); // selection itself causes falling edge

    r.write8(0xFF0F, 0);
    r.set_joypad_buttons(0x01); // A pressed, but only direction row selected
    assert((r.read8(0xFF00) & 0x0Fu) == 0x0Fu);
    assert((r.read8(0xFF0F) & 0x10u) == 0);

    r.write8(0xFF00, 0x00); // both rows selected; held A is now visible
    assert((r.read8(0xFF00) & 0x0Fu) == 0x0Eu);
    assert((r.read8(0xFF0F) & 0x10u) != 0);
    assert(r.get_hl() == 0xC123);
    std::cout << "Runtime register/ALU/MBC5 smoke tests passed.\n";

    // CGB PPU: tile decoding, CGB CRAM, LCD modes, memory locks and VBlank.
    auto& video = fixtures->video;
    setup_cgb(video);
    assert(video.ppu.cgb_mode());
    set_palette(video,0xFF68,{0x7FFF,0x001F,0x03E0,0x7C00});

    // CGB VBK selects the CPU-visible VRAM bank without changing the PPU's
    // independent attribute-selected bank reads.
    auto& banks = fixtures->banks;
    setup_cgb(banks);
    banks.vram[0]=0x12;
    banks.vram[0x2000]=0x34;
    assert(banks.read8(0x8000)==0x12);
    banks.write8(0xFF4F,1);
    assert(banks.read8(0x8000)==0x34);
    assert(banks.read8(0xFF4F)==0xFF); // FF4F returns 0xFE | VBK.
    banks.write8(0xFF4F,0);
    assert(banks.read8(0x8000)==0x12);
    assert(banks.read8(0xFF4F)==0xFE);
    // Tile 0: its leftmost pixel is colour 1 (red); the others are colour 0.
    video.vram[0x0000]=0x80;
    video.vram[0x0001]=0x00;
    video.vram[0x1800]=0;
    video.write8(0xFF40,0x91); // LCD on, unsigned tiles, BG map $9800, BG priority enabled.
    assert((video.read8(0xFF41)&3)==2);
    video.advance_cycles(80);
    assert((video.read8(0xFF41)&3)==3);
    assert(video.read8(0x8000)==0xFF); // VRAM is blocked during Mode 3.
    assert(video.read8(0xFF69)==0xFF);  // CGB palette data is also blocked in Mode 3.
    video.write8(0x8000,0x00);         // The blocked write must not change tile data.
    video.write8(0xFF68,0x80);          // Auto-increment index remains writable in Mode 3.
    video.write8(0xFF69,0x00);          // Blocked CRAM write still advances BGPI on CGB.
    assert(video.read8(0xFF68)==0x81);
    video.advance_cycles(172);
    assert((video.read8(0xFF41)&3)==0);
    assert(video.read8(0x8000)==0x80);
    assert(video.ppu.framebuffer()[0]==0xFF0000FFu); // CGB palette 0 colour 1 = red.
    assert(video.ppu.framebuffer()[1]==0xFFFFFFFFu); // CGB palette 0 colour 0 = white.
    assert(video.read8(0xFF44)==0);

    // OAM is also inaccessible in Mode 2 and Mode 3, then returns in HBlank.
    auto& bus_lock = fixtures->bus_lock;
    setup_cgb(bus_lock);
    bus_lock.oam[0]=0x22;
    bus_lock.write8(0xFF40,0x91);
    assert(bus_lock.read8(0xFE00)==0xFF);
    bus_lock.write8(0xFE00,0x99);
    assert(bus_lock.oam[0]==0x22);
    bus_lock.advance_cycles(80+172);
    assert(bus_lock.read8(0xFE00)==0x22);

    // PPU raises VBlank after line 143 and exposes a completed-frame signal.
    video.advance_cycles(204 + (143u * 456u));
    assert(video.read8(0xFF44)==144);
    assert((video.read8(0xFF41)&3)==1);
    assert((video.read8(0xFF0F)&0x01)!=0);
    assert(video.ppu.frame_ready());
    assert(video.ppu.consume_frame_ready());
    assert(!video.ppu.frame_ready());

    // CGB object composition, including 8x8 OBJ decode and palette selection.
    auto& objects = fixtures->objects;
    setup_cgb(objects);
    set_palette(objects,0xFF68,{0x7FFF,0x001F,0x03E0,0x7C00});
    set_palette(objects,0xFF6A,{0x7FFF,0x7C00,0x03E0,0x001F});
    objects.vram[16]=0x80; // Tile 1 left pixel = colour 1.
    objects.vram[17]=0x00;
    objects.oam[0]=16; // Sprite top at scanline 0.
    objects.oam[1]=8;  // Sprite left at screen x=0.
    objects.oam[2]=1;
    objects.oam[3]=0;
    objects.write8(0xFF40,0x93); // LCD + OBJ + BG.
    objects.advance_cycles(456); // Complete line 0.
    assert(objects.ppu.framebuffer()[0]==0xFFFF0000u); // OBJ palette colour 1 = blue.

    // A BG map priority attribute hides a non-transparent OBJ pixel in CGB mode.
    auto& priority = fixtures->priority;
    setup_cgb(priority);
    set_palette(priority,0xFF68,{0x7FFF,0x001F,0x03E0,0x7C00});
    set_palette(priority,0xFF6A,{0x7FFF,0x7C00,0x03E0,0x001F});
    priority.vram[0]=0x80;  // BG tile 0 pixel 0 = colour 1.
    priority.vram[1]=0;
    priority.vram[0x1800]=0;
    priority.vram[0x2000+0x1800]=0x80; // BG tile attribute: priority.
    priority.vram[16]=0x80; // OBJ tile 1 pixel 0 = colour 1.
    priority.vram[17]=0;
    priority.oam[0]=16; priority.oam[1]=8; priority.oam[2]=1; priority.oam[3]=0;
    priority.write8(0xFF40,0x93);
    priority.advance_cycles(456);
    assert(priority.ppu.framebuffer()[0]==0xFF0000FFu); // BG wins over blue OBJ.

    // CGB attribute maps select the second tile-data bank, flip horizontally,
    // and select an independent BG palette.
    auto& attributes = fixtures->attributes;
    setup_cgb(attributes);
    set_palette_at(attributes,0xFF68,0x00,{0x7FFF,0x001F,0x03E0,0x7C00});
    set_palette_at(attributes,0xFF68,0x08,{0x7FFF,0x03E0,0x001F,0x7C00});
    attributes.vram[0x1800]=1;       // BG map entry selects tile 1.
    attributes.vram[0x3800]=0x29;  // X flip + VRAM bank 1 + BG palette 1.
    attributes.vram[0x2000+16]=0x01; // Only source pixel 7 is colour index 1.
    attributes.vram[0x2000+17]=0x00;
    attributes.write8(0xFF40,0x91);
    attributes.advance_cycles(456);
    assert(attributes.ppu.framebuffer()[0]==0xFF00FF00u); // Flipped bank-1 pixel, palette 1 green.
    assert(attributes.ppu.framebuffer()[1]==0xFFFFFFFFu);

    // Window uses its own tile map and a separate line counter.
    auto& window = fixtures->window;
    setup_cgb(window);
    set_palette(window,0xFF68,{0x7FFF,0x001F,0x03E0,0x7C00});
    window.vram[16]=0x80; window.vram[17]=0; // Tile 1 colour 1.
    window.vram[0x1C00]=1; // Window map $9C00.
    window.write8(0xFF4A,0);
    window.write8(0xFF4B,7);
    window.write8(0xFF40,0xF1); // LCD, window map, window, unsigned tiles, BG/window.
    window.advance_cycles(456);
    assert(window.ppu.framebuffer()[0]==0xFF0000FFu);

    // BG palette auto-increment wraps within its 64-byte CRAM address space.
    window.write8(0xFF68,0xBF);
    window.write8(0xFF69,0x12);
    assert(window.read8(0xFF68)==0x80);

    // DMG compatibility path: LCDC.0 blanks BG to white but OBJ remains visible;
    // DMG OBJ priority is smaller X first, then OAM index.
    auto& dmg = fixtures->dmg;
    dmg.rom.assign(0x8000,0);
    dmg.rom[0x143]=0x00;
    dmg.ppu.reset(dmg);
    dmg.vram[16]=0xFF; dmg.vram[17]=0; // Tile 1: all pixels are colour index 1.
    dmg.vram[32]=0; dmg.vram[33]=0xFF; // Tile 2: all pixels are colour index 2.
    // OAM index 0 is farther right; DMG must prioritize index 1's smaller X.
    dmg.oam[0]=16; dmg.oam[1]=12; dmg.oam[2]=1; dmg.oam[3]=0;
    dmg.oam[4]=16; dmg.oam[5]=8;  dmg.oam[6]=2; dmg.oam[7]=0;
    dmg.write8(0xFF47,0xE4); // Map BG color 1 -> light gray and color 2 -> dark gray.
    dmg.write8(0xFF48,0xE4); // Same map for OBJ palette 0.
    dmg.write8(0xFF40,0x92); // LCD + OBJ, BG disabled.
    dmg.advance_cycles(456);
    assert(dmg.ppu.framebuffer()[0]==0xFF606060u); // Smaller-X sprite (tile 2) is on top.
    assert(dmg.ppu.framebuffer()[4]==0xFF606060u); // The overlap keeps the smaller-X sprite.
    assert(dmg.ppu.framebuffer()[10]==0xFFC0C0C0u); // Only farther-right tile 1 remains.
    assert(dmg.ppu.framebuffer()[20]==0xFFFFFFFFu); // BG disabled means white, not BGP colour 0.
    assert(dmg.read8(0xFF68)==0xFF); // CGB palette registers are absent in DMG mode.

    // STAT mode interrupts are edge-triggered as the PPU enters enabled modes.
    auto& stat = fixtures->stat;
    setup_cgb(stat);
    stat.write8(0xFF41,0x28); // Enable both Mode 2 and Mode 0 STAT sources.
    stat.write8(0xFF40,0x91);
    assert((stat.read8(0xFF0F)&0x02)!=0); // Mode 2 entry.
    stat.write8(0xFF0F,0);
    stat.advance_cycles(80+172);
    assert((stat.read8(0xFF0F)&0x02)!=0); // Mode 0 entry.
    stat.advance_cycles(204);
    assert(stat.ppu.mode()==2); // Next scanline's OAM search.

    // Mode 3 length includes SCX fine-scroll, window setup and OBJ fetch stalls.
    auto& penalties = fixtures->penalties;
    setup_cgb(penalties);
    penalties.write8(0xFF43,7);
    penalties.write8(0xFF4A,0);
    penalties.write8(0xFF4B,7);
    penalties.oam[0]=16; penalties.oam[1]=8; penalties.oam[2]=0; penalties.oam[3]=0;
    penalties.write8(0xFF40,0xB3); // LCD + Window + OBJ + BG.
    penalties.advance_cycles(80+195);
    assert(penalties.ppu.mode()==3);
    penalties.advance_cycles(1);
    assert(penalties.ppu.mode()==0);

    // The emitted-instruction cycle guard advances the PPU even when an
    // instruction returns from inside its generated scope.
    auto& clocked = fixtures->clocked;
    setup_cgb(clocked);
    clocked.write8(0xFF40,0x91);
    {
        auto instruction_cycles = clocked.defer_cycles(80);
        (void)instruction_cycles;
    }
    assert(clocked.cpu.cycles==80);
    assert(clocked.ppu.mode()==3);

    std::cout << "CGB/DMG PPU composition, palettes, locks, interrupts and timing tests passed.\n";

    // The dynamic-target fallback executes SM83 instructions not represented
    // by the static block graph, and preserves the same CPU/PPU timing path.
    auto& interpreted=fixtures->interpreted;
    interpreted.rom.assign(0x8000,0);
    interpreted.ppu.reset(interpreted);
    interpreted.cpu.pc=0x0100;
    const std::array<std::uint8_t,12> program={
        0x3E,0x11,       // LD A,$11
        0x06,0x22,       // LD B,$22
        0x80,            // ADD A,B
        0xEA,0x00,0xC0,  // LD ($C000),A
        0xC3,0x10,0x01,  // JP $0110
        0x00             // Padding
    };
    std::copy(program.begin(),program.end(),interpreted.rom.begin()+0x0100);
    std::string interpreter_error;
    assert(interpreted.execute_interpreted_instruction(interpreter_error));
    assert(interpreted.cpu.a==0x11 && interpreted.cpu.pc==0x0102);
    assert(interpreted.execute_interpreted_instruction(interpreter_error));
    assert(interpreted.cpu.b==0x22 && interpreted.cpu.pc==0x0104);
    assert(interpreted.execute_interpreted_instruction(interpreter_error));
    assert(interpreted.cpu.a==0x33 && interpreted.cpu.pc==0x0105);
    assert(interpreted.execute_interpreted_instruction(interpreter_error));
    assert(interpreted.read8(0xC000)==0x33 && interpreted.cpu.pc==0x0108);
    assert(interpreted.execute_interpreted_instruction(interpreter_error));
    assert(interpreted.cpu.pc==0x0110);

    // The divider falling edge clocks TIMA, and overflow requests the timer IRQ.
    auto& timer=fixtures->timer;
    timer.rom.assign(0x8000,0);
    timer.ppu.reset(timer);
    timer.write8(0xFF05,0xFE);
    timer.write8(0xFF06,0x42);
    timer.write8(0xFF07,0x05); // Enable, divider bit 3 (one increment per 16 T-cycles).
    timer.advance_cycles(16);
    assert(timer.read8(0xFF05)==0xFF);
    timer.advance_cycles(16);
    assert(timer.read8(0xFF05)==0x00); // Overflow starts the four-cycle reload delay.
    timer.advance_cycles(4);
    assert(timer.read8(0xFF05)==0x42);
    assert((timer.read8(0xFF0F)&0x04)!=0);

    // A disconnected Game Boy Link Cable pulls the incoming serial line high.
    // Internal-clock transfers complete with SB=$FF and request INT $58;
    // external-clock transfers stay pending because no peer supplies edges.
    auto& serial=fixtures->serial;
    setup_cgb(serial);
    serial.write8(0xFF01,0xE1);
    serial.write8(0xFF02,0x81); // Internal clock, normal rate.
    serial.advance_cycles(4095);
    assert((serial.read8(0xFF02)&0x80)!=0);
    assert((serial.read8(0xFF0F)&0x08)==0);
    serial.advance_cycles(1);
    assert(serial.read8(0xFF01)==0xFF);
    assert((serial.read8(0xFF02)&0x80)==0);
    assert((serial.read8(0xFF0F)&0x08)!=0);

    serial.write8(0xFF0F,0);
    serial.write8(0xFF01,0xE0);
    serial.write8(0xFF02,0x80); // External clock: no cable, no transfer completion.
    serial.advance_cycles(8192);
    assert(serial.read8(0xFF01)==0xE0);
    assert((serial.read8(0xFF02)&0x80)!=0);
    assert((serial.read8(0xFF0F)&0x08)==0);

    serial.write8(0xFF02,0x83); // CGB fast internal clock: 16 CPU cycles per bit.
    serial.advance_cycles(127);
    assert((serial.read8(0xFF02)&0x80)!=0);
    serial.advance_cycles(1);
    assert(serial.read8(0xFF01)==0xFF);
    assert((serial.read8(0xFF02)&0x80)==0);
    assert((serial.read8(0xFF0F)&0x08)!=0);

    // Interrupt dispatch pushes the current PC and enters the highest-priority vector.
    auto& interrupt=fixtures->interrupt;
    interrupt.rom.assign(0x8000,0);
    interrupt.ppu.reset(interrupt);
    interrupt.cpu.pc=0x1234;
    interrupt.cpu.sp=0xFFFE;
    interrupt.cpu.ime=true;
    interrupt.interrupt_enable=0x05;
    interrupt.io[0x0F]=0x05;
    assert(interrupt.service_interrupt());
    assert(interrupt.cpu.pc==0x0040 && interrupt.cpu.sp==0xFFFC);
    assert(interrupt.read8(0xFFFC)==0x34 && interrupt.read8(0xFFFD)==0x12);
    assert((interrupt.io[0x0F]&1)==0 && (interrupt.io[0x0F]&4)!=0);

    std::cout << "Dynamic SM83 fallback, timer and interrupt dispatch tests passed.\n";

    // NR52 reports the APU power and real channel-enable flags. A channel
    // trigger is ignored with its DAC disabled, and a length-expiry event
    // clears the corresponding NR52 status bit.
    auto& apu = fixtures->apu;
    setup_cgb(apu);
    assert(apu.read8(0xFF26) == 0xF0);
    apu.write8(0xFF12,0xF0); // CH1 DAC enabled.
    apu.write8(0xFF11,0x3F); // Length = 1.
    apu.write8(0xFF14,0xC0); // Trigger and enable length counter.
    assert((apu.read8(0xFF26)&0x01)!=0);
    apu.advance_cycles(8192);
    assert((apu.read8(0xFF26)&0x01)==0);
    apu.write8(0xFF14,0x80); // Trigger again while DAC remains enabled.
    assert((apu.read8(0xFF26)&0x01)!=0);
    apu.write8(0xFF12,0x00); // DAC off immediately clears CH1 status.
    assert((apu.read8(0xFF26)&0x01)==0);
    apu.write8(0xFF26,0x00);
    assert((apu.read8(0xFF26)&0x80)==0);
    apu.write8(0xFF12,0xF0); // Non-length registers ignore writes while powered off.
    apu.write8(0xFF26,0x80);
    assert(apu.read8(0xFF12)==0);

    // GDMA copies a complete block to the selected VRAM bank and stalls the
    // CPU for the documented 8 M-cycles (32 T-cycles) in normal speed.
    auto& gdma = fixtures->gdma;
    setup_cgb(gdma);
    for (unsigned i=0;i<16;++i) gdma.rom[0x0100+i]=static_cast<std::uint8_t>(0xA0+i);
    gdma.write8(0xFF40,0x00); // LCD off: safe to perform a full GDMA transfer.
    gdma.write8(0xFF4F,1);
    gdma.write8(0xFF51,0x01); gdma.write8(0xFF52,0x0F); // Low nibble is ignored.
    gdma.write8(0xFF53,0x00); gdma.write8(0xFF54,0x0F);
    assert(gdma.read8(0xFF51)==0xFF && gdma.read8(0xFF54)==0xFF); // Write-only registers.
    gdma.write8(0xFF55,0x00); // One 16-byte general-purpose block.
    for (unsigned i=0;i<16;++i)
        assert(gdma.vram[0x2000+i]==static_cast<std::uint8_t>(0xA0+i));
    assert(gdma.read8(0xFF55)==0xFF);
    gdma.advance_cycles(4);
    assert(gdma.cpu.cycles==36); // 4 instruction cycles + 32 DMA stall cycles.

    // HDMA copies exactly one block at each visible HBlank, then completes
    // and returns FF from HDMA5. Each block also stalls 32 CPU T-cycles.
    auto& hdma = fixtures->hdma;
    setup_cgb(hdma);
    for (unsigned i=0;i<32;++i) hdma.rom[0x0100+i]=static_cast<std::uint8_t>(0x40+i);
    hdma.write8(0xFF51,0x01); hdma.write8(0xFF52,0x00);
    hdma.write8(0xFF53,0x00); hdma.write8(0xFF54,0x20);
    hdma.write8(0xFF55,0x81); // Two 16-byte HBlank DMA blocks.
    assert(hdma.read8(0xFF55)==0x01);
    for (unsigned i=0;i<16;++i) assert(hdma.vram[0x20+i]==0);
    hdma.advance_cycles(252); // Mode 2 + Mode 3 -> first visible HBlank.
    assert(hdma.read8(0xFF55)==0x00);
    for (unsigned i=0;i<16;++i) assert(hdma.vram[0x20+i]==static_cast<std::uint8_t>(0x40+i));
    hdma.advance_cycles(424); // Advance to line 1 HBlank.
    assert(hdma.read8(0xFF55)==0xFF);
    for (unsigned i=0;i<16;++i) assert(hdma.vram[0x30+i]==static_cast<std::uint8_t>(0x50+i));

    auto& hdma_halt = fixtures->hdma_halt;
    setup_cgb(hdma_halt);
    hdma_halt.rom[0x0100]=0xA5;
    hdma_halt.write8(0xFF51,0x01); hdma_halt.write8(0xFF52,0x00);
    hdma_halt.write8(0xFF53,0x00); hdma_halt.write8(0xFF54,0x40);
    hdma_halt.write8(0xFF55,0x80); // One HBlank block.
    hdma_halt.cpu.halted=true;
    hdma_halt.advance_cycles(252);
    assert(hdma_halt.read8(0xFF55)==0x00); // Still active, not transferred while halted.
    assert(hdma_halt.vram[0x40]==0);
    hdma_halt.cpu.halted=false;
    hdma_halt.advance_cycles(456); // Next visible line's HBlank.
    assert(hdma_halt.read8(0xFF55)==0xFF);
    assert(hdma_halt.vram[0x40]==0xA5);

    std::cout << "NR52 APU status, CGB GDMA and HBlank DMA tests passed.\n";

    // Native blocks must yield at instruction boundaries for VBlank frames,
    // HALT/STOP and enabled interrupts rather than running past a presentation
    // or interrupt scheduling point.
    {
        auto& safe = fixtures->serial;
        safe.cpu.faulted=false; safe.cpu.halted=false; safe.cpu.stopped=false;
        safe.cpu.ime=false; safe.interrupt_enable=0; safe.io[0x0F]=0;
        assert(!safe.should_yield_native_block());
        safe.cpu.halted=true;
        assert(safe.should_yield_native_block());
        safe.cpu.halted=false; safe.cpu.ime=true;
        safe.interrupt_enable=0x01; safe.io[0x0F]=0x01;
        assert(safe.should_yield_native_block());
        safe.cpu.ime=false; safe.interrupt_enable=0; safe.io[0x0F]=0;
        safe.cpu.stopped=true;
        assert(safe.should_yield_native_block());
        safe.cpu.stopped=false;
        safe.double_speed=false;
        safe.advance_cycles(70224);
        assert(safe.ppu.frame_ready());
        assert(safe.should_yield_native_block());
    }

    // End-to-end cartridge execution: a tiny CGB program disables LCD, writes
    // a tile through the CPU bus, enables LCD, and loops. GameRunner must run
    // the CPU fallback, advance the PPU to VBlank, and expose the real pixels.
    auto rom_path = std::filesystem::temp_directory_path() / "oracles-live-frame-test.gbc";
    std::vector<std::uint8_t> test_rom(0x8000,0);
    test_rom[0x143]=0x80; // CGB-compatible cartridge.
    test_rom[0x147]=0x1B; // MBC5 + RAM + battery mapper header.
    test_rom[0x148]=0x00; // 32 KiB ROM.
    test_rom[0x149]=0x02; // 8 KiB SRAM, as in the Oracle retail cartridges.
    test_rom[0x0000]=0x42; // Marker used to verify 64-bank MBC5 address-line mirroring.
    test_rom[0x4000]=0x99;
    const std::array<std::uint8_t,20> draw_program={
        0x3E,0x00,             // LD A,$00
        0xE0,0x40,             // LDH ($FF40),A -- disable LCD while writing VRAM
        0x3E,0xFF,             // LD A,$FF
        0xEA,0x00,0x80,        // LD ($8000),A -- tile 0 row 0, colour 1 across
        0x3E,0x00,             // LD A,$00
        0xEA,0x01,0x80,        // LD ($8001),A
        0x3E,0x91,             // LD A,$91
        0xE0,0x40,             // LDH ($FF40),A -- enable LCD/BG
        0x18,0xFE              // JR back to itself
    };
    std::copy(draw_program.begin(),draw_program.end(),test_rom.begin()+0x0100);
    {
        std::ofstream file(rom_path,std::ios::binary|std::ios::trunc);
        assert(file.good());
        file.write(reinterpret_cast<const char*>(test_rom.data()),static_cast<std::streamsize>(test_rom.size()));
        assert(file.good());
    }
    auto& live=fixtures->live_game;
    std::string live_error;
    assert(live.load(rom_path,nullptr,live_error));
    auto live_status=oracles::FrameRunStatus::Running;
    for(unsigned slice=0;slice<4 && live_status==oracles::FrameRunStatus::Running;++slice)
        live_status=live.run_slice(0,live_error);
    assert(live_status==oracles::FrameRunStatus::FrameReady);
    assert(live.frame_count()==1);
    assert(live.interpreted_instruction_count()>0);
    assert(live.runtime().ppu.framebuffer()[0]!=0xFFFFFFFFu);
    assert(live.runtime().ppu.framebuffer()[oracles::Ppu::kScreenWidth]==0xFFFFFFFFu);
    // The cartridge is MBC5 with one physical 8 KiB SRAM bank. Upper ROM
    // address lines beyond the physical ROM mirror, and RAM bank-register
    // writes cannot select storage that is not wired into the cartridge.
    live.runtime().write8(0x2000,0x40);
    assert(live.runtime().read8(0x4000)==0x42);
    live.runtime().write8(0x0000,0x0A);
    live.runtime().write8(0xA000,0x5A);
    assert(live.runtime().read8(0xA000)==0x5A);
    live.runtime().write8(0x4000,0x01);
    assert(live.runtime().read8(0xA000)==0x5A);
    std::error_code remove_error;
    std::filesystem::remove(rom_path,remove_error);
    std::cout << "Live cartridge CPU-to-PPU framebuffer integration passed.\n";

}
