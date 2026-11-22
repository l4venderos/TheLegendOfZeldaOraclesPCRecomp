# PPU and live-render implementation notes — v0.2.1

This project contains an independently authored scanline-oriented DMG/CGB PPU in `source/ppu/ppu.cpp`. The PPU is driven by the same runtime used by generated native game code and the Windows D3D11 frontend. Hardware descriptions were used as research references; no emulator/recompiler PPU source was copied or made a build dependency.

## Implemented PPU behavior

- 456 dots per scanline, 144 visible scanlines and 10 VBlank scanlines.
- LCD modes 0 (HBlank), 1 (VBlank), 2 (OAM search) and 3 (pixel transfer).
- Base Mode 2 length of 80 dots and Mode 3 base length of 172 dots, with an approximation for SCX fine-scroll, window and sprite-fetch penalties bounded by the documented 289-dot maximum.
- STAT coincidence/mode interrupt line edge detection and VBlank IF request.
- LCDC, STAT, SCY, SCX, LY, LYC, BGP, OBP0, OBP1, WY and WX.
- CGB VBK, BGPI/BGPD, OBPI/OBPD and OPRI register access rules.
- 32×32 BG/window tile maps at VRAM offsets 0x1800 and 0x1C00; unsigned 0x8000 and signed 0x9000 tile addressing.
- CGB bank-1 tile attributes: BG priority, X/Y flip, tile data bank and palette number.
- CGB RGB555 BG/OBJ palettes; DMG BGP/OBP shade mapping.
- 8×8 and 8×16 sprites, first-ten-per-line selection, transparent colour index zero, X/Y flip, CGB OBJ bank/palette and BG/OBJ priority.
- CPU VRAM lock during Mode 3; CPU OAM lock during Modes 2 and 3; CGB palette data port lock during Mode 3.
- A 160×144 `0xAABBGGRR` framebuffer with VBlank frame-ready signalling.

## Runtime-to-renderer connection

- `GameRunner` calls the selected Ages or Seasons generated native dispatcher for statically discovered `(ROM bank, PC)` blocks.
- Uncovered and dynamic code locations use the in-project SM83 instruction interpreter; no external emulator core is called.
- Generated native blocks yield at instruction boundaries when a frame is ready, an enabled interrupt must be serviced, or HALT/STOP is entered.
- `Runtime::advance_cycles` advances CPU timers, serial state, the partial APU frame sequencer and the PPU. PPU dot timing is divided by two in CGB double-speed mode.
- The runtime invokes `Runtime::on_hblank()` when the PPU enters Mode 0 on a visible line. This is used by the CGB HBlank DMA transfer scheduler.
- On VBlank, the Win32 frontend submits the actual PPU RGBA framebuffer to the D3D11 renderer, which uploads it to a GPU texture and presents through a DXGI swap chain.
- The portable `Oracles` executable uses the same GameRunner/runtime and writes the framebuffer to PPM for non-Windows inspection.

## CGB transfer and sound-status support

- `FF51`–`FF55`: CGB GDMA and HBlank HDMA register handling, address masks, selected VRAM bank, 16-byte block copies, per-block CPU stall cycles, transfer status and HBlank pausing while the CPU is halted.
- `FF26` / NR52: APU power and read-only channel-on flags, DAC gating and basic length-counter sequencing. This is only a status model; it does not synthesize waveforms or output audio.
- `FF46` OAM DMA still uses an immediate memory copy rather than its full timed bus model.

## Explicit accuracy boundary

The PPU uses a scanline composition model. It captures a line when Mode 3 ends and uses the register/VRAM/OAM state then. It is not a dot-FIFO/fetcher implementation. Exact per-dot fetch state, mid-scanline raster effects, all hardware quirks, Mode 3 penalty corner cases, the LY=153 quirk, the blank first-frame behavior after LCD re-enable, and precise DMA/PPU bus arbitration are not yet modeled. CGB GDMA/HDMA behavior is implemented at transfer-block and scanline boundaries, not claimed as cycle-perfect silicon timing. Audio synthesis, DAC mixing and audio-device output are not implemented.

The frontend integration has not yet been validated against the retail Ages and Seasons ROMs on Windows/MSVC in this development environment. The included synthetic native-code fixtures verify the CPU→PPU→framebuffer path but are not the commercial games and do not prove full compatibility.

## Regression coverage

`oracles-runtime-tests` exercises tile decoding, CGB palette reads/writes and auto-increment, BG/window map selection, object composition, CGB BG priority, DMG BG blanking and OBJ priority, LCD mode transitions, STAT/VBlank signals, VRAM/OAM access locks, NR52 power/channel flags, GDMA copy/stall behavior, HDMA HBlank scheduling and HALT pausing, serial/timer behavior, interpreter fallback and a complete synthetic cartridge frame. `oracles-emitter-regression-generator` ensures emitted instructions use deferred cycle scopes and conditional branches emit path-dependent timing expressions.
