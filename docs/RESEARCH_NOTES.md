# Public research references (not implementation dependencies)

This project was authored independently. The repositories and documents below were reviewed to understand the existing landscape, known Game Boy static-recompilation architecture, and Game Boy Color hardware requirements. **No source files, generated source, runtime library, submodule, build artifacts, or executable from these projects was copied or included.**

## Existing Oracle-specific projects

- `sp00nznet/oracle-recompiled` — <https://github.com/sp00nznet/oracle-recompiled>. Its own README describes the Oracle project as thin game-specific wrappers and identifies `gbrecomp/` as a submodule; it says that the shared SM83 recompiler, CPU model, CGB PPU, MBC5, APU and SDL platform layer come from `sp00nznet/gb-recompiled`. We reviewed the README and repository layout to identify the external dependency chain. This project intentionally does not use that submodule or its source. (Research snapshot: 2026-11-21.)
- `ZakyPew/epoch-equinox` — <https://github.com/ZakyPew/epoch-equinox>. Its README describes an earlier static-generation phase, but also documents an `interp_probe.c` investigation that found the generated native code was not being executed in that earlier revision; the generation stage was removed and the remaining player interprets the supplied ROM at runtime. We treated this as a useful cautionary example: this project has a startup probe that actually calls its generated block functions, though it does not yet run the complete game. No source code was copied.
- `omegakatana92/The-Legend-of-Zelda-Oracles-of-Seasons-Recomp` — <https://github.com/omegakatana92/The-Legend-of-Zelda-Oracles-of-Seasons-Recomp>. Reviewed as an additional game-specific recompilation repository.
- `Stewmath/oracles-disasm` — <https://github.com/Stewmath/oracles-disasm>. A disassembly reference surfaced during research; this project does not ingest its disassembly as a build input.

## Generic Game Boy recompilation research

- `sp00nznet/gb-recompiled` — <https://github.com/sp00nznet/gb-recompiled>. We inspected its public README and the public `recompiler/src/decoder.cpp` and `recompiler/src/rom.cpp` source views to understand the scope of a mature decoder/runtime: banked ROM decode, C code generation, bank-aware control flow, memory-mapped IO, PPU/APU, and known dynamic-jump/code-discovery limitations. No source text was copied. Its README also explicitly cautions that successful recompilation does not necessarily mean a game is fully playable.
- `mstan/gbrecompiled` — <https://github.com/mstan/gbrecompiled>. Reviewed as a development fork with descriptions of static analysis, code generation, bank-switch tracking and runtime responsibilities.
- `agnt-gg/gb-pokemon-rom-to-wasm` — <https://github.com/agnt-gg/gb-pokemon-rom-to-wasm>. Reviewed as a separate experimental ROM-to-WASM approach; it is not a dependency and no code was reused.

## Hardware specification

- Pan Docs, actively maintained Game Boy / Game Boy Color technical reference: <https://gbdev.io/pandocs/>.
- Pan Docs source repository (CC0-1.0): <https://github.com/gbdev/pandocs>.

Pan Docs is the main specification reference for the SM83 instruction set, memory map, cartridge banking, CGB registers, LCD/PPU, audio, timers, DMA and interrupts. The source tree in this ZIP contains independently written implementation code; it does not embed Pan Docs text.


## Windows display and GPU-selection APIs

- Microsoft Learn, `IDXGIFactory1::EnumAdapters1`: <https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory1-enumadapters1>. Used as the API reference for enumerating installed DXGI adapters.
- Microsoft Learn, `IDXGIAdapter1`: <https://learn.microsoft.com/en-us/windows/desktop/api/dxgi/nn-dxgi-idxgiadapter1>. Used as the API reference for obtaining adapter descriptions.
- Microsoft Learn, Direct3D 11 adapter enumeration: <https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-enum>. Documents selecting an adapter and passing it to D3D device creation.
- Microsoft Learn, `IDXGIFactory6::EnumAdapterByGpuPreference`: <https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_6/nf-dxgi1_6-idxgifactory6-enumadapterbygpupreference>. Reviewed as an optional preference-based ordering API; this initial implementation uses explicit adapter selection through `EnumAdapters1` for broad compatibility.

These documents were consulted as API references only. The project does not copy or vendor an external renderer or GPU-selection implementation.

## Direct3D 11 rendering references (2026-11-21)

- Microsoft Learn, [How to enumerate adapters](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-enum) and [D3D11CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice): consulted for selecting an explicit DXGI adapter and the `D3D_DRIVER_TYPE_UNKNOWN` requirement when a non-null adapter is supplied.
- Microsoft Learn, [IDXGIFactory2::CreateSwapChainForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd) and [DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model): consulted for HWND swap-chain creation and flip-model presentation.
- Microsoft Learn, [D3DCompile](https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile): consulted for compiling the small in-project HLSL shaders at runtime.
- Microsoft Learn, [How to initialize a texture programmatically](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-textures-how-to-fill-manually): consulted for updating the D3D11 texture used by the frame-upload API.

These pages are API references only. The D3D11 renderer implementation in this project is authored in `source/renderer/d3d11_renderer.cpp`; no third-party renderer was copied or vendored.

## Game Boy / Game Boy Color PPU implementation references (2026-11-21)

- Pan Docs, [Rendering](https://gbdev.io/pandocs/Rendering.html): consulted for 456-dot lines, 144 VDraw lines, 10 VBlank lines, LCD modes, CPU video-memory access windows and the 172–289-dot Mode 3 range.
- Pan Docs, [Tile Maps](https://gbdev.io/pandocs/Tile_Maps.html): consulted for 32×32 BG/window maps and the CGB tile-attribute byte (priority, flips, VRAM bank and palette selection).
- Pan Docs, [LCDC](https://gbdev.io/pandocs/LCDC.html): consulted for LCDC BG/window/OBJ control and the DMG-vs-CGB meaning of LCDC.0.
- Pan Docs, [Palettes](https://gbdev.io/pandocs/Palettes.html): consulted for DMG BGP/OBP mapping, 8×4 CGB BG/OBJ palettes, FF68–FF6B palette-port behavior and auto-increment.
- Open Game Boy Documentation Project (mGBA), [Picture Processing Unit](https://mgba-emu.github.io/gbdoc/): cross-reference for line timing, mode ordering, mode duration and memory-access restrictions.

These references are specifications/documentation only. No PPU implementation code from Pan Docs, mGBA or another emulator was copied or vendored.


## Live execution, MBC5 and serial hardware references (2026-11-22)

- Pan Docs, [Serial Data Transfer](https://gbdev.io/pandocs/Serial_Data_Transfer_%28Link_Cable%29.html): consulted for FF01/SB, FF02/SC, clock source selection, CGB fast transfer rates, disconnected-link input being pulled high, and the serial interrupt. The runtime models an unconnected cable as incoming ones; an external-clock transfer remains waiting without a peer.
- Pan Docs, [MBC5](https://gbdev.io/pandocs/MBC5.html): consulted for the 9-bit ROM bank register and external RAM bank selection. Cartridge RAM header size is used to map only physically present RAM; an 8 KiB RAM chip has one visible bank.
- Microsoft Learn, [D3D11 device enumeration](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-enum), [D3D11CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice), and [IDXGISwapChain::Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present): reviewed for the existing hardware-adapter creation and frame presentation API path.

These were technical references only. The GameRunner, CPU interpreter, serial behavior, CGB/DMG PPU, frame scheduling and D3D11 renderer remain independently authored project code; no recompilation or emulator source was copied.


## CGB DMA and APU status research (2026-11-22)

- Pan Docs, [CGB Registers](https://gbdev.io/pandocs/CGB_Registers.html): consulted for `FF51`–`FF55` source/destination masking, GDMA behavior, HBlank DMA's 16-byte blocks, visible-line-only transfer, CPU stalls, HALT pausing, cancellation and status semantics. The runtime implements these modes at block/scanline boundaries; exact electrical bus arbitration and unusual overflow cases remain unverified.
- Pan Docs, [Audio Registers](https://gbdev.io/pandocs/Audio_Registers.html): consulted for `NR52` power bit, read-only channel-on flags, DAC gating and length-counter behavior. The runtime implements only a partial status/length model; it does not implement audio waveforms or mixing.

These hardware references were used to author the new register and transfer logic independently. No emulator code was copied.
