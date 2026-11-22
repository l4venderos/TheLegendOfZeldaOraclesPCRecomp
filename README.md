# Oracles PC Recomp

**Independent development project — v0.2.4 (work branch)**

An independently authored C++ static-recompilation project for **The Legend of Zelda: Oracle of Ages** and **The Legend of Zelda: Oracle of Seasons**. It does not wrap another Oracle recompilation, include a third-party emulator library, or distribute commercial ROMs.

> **Current status:** the live execution path is now connected. The Windows frontend loads the selected user-supplied ROM, dispatches generated native SM83 blocks, falls back to the in-tree SM83 interpreter for undiscovered/dynamic targets, advances timers/serial/PPU state, and submits completed PPU RGBA frames to the selected Direct3D 11 GPU. This is a genuine CPU-to-PPU-to-renderer path, not a diagnostic image. It is still an active development build and is **not yet compatibility-certified as a fully playable Oracle release**; see the limitations below.

## ZIP layout

```text
Oracles-PC-Recomp/
├── source/
│   ├── app/                  # Windows frontend and portable headless frame dumper
│   ├── compiler/             # ROM parser, SM83 decoder, CFG analysis, C++ emitter
│   ├── include/oracles/      # project-owned runtime and D3D11 interfaces
│   ├── renderer/             # D3D11 renderer and shared aspect-layout math
│   ├── runtime/              # CPU, MBC5, serial, timer, scheduler and game runner
│   ├── ppu/                  # independently written CGB/DMG scanline PPU
│   ├── tests/                # compiler, runtime, PPU, emitter, layout and keyboard-binding tests
│   └── CMakeLists.txt
├── GameData/                 # add your own Ages and Seasons ROMs here
├── docs/                     # research and architecture notes
├── compile.bat                # Windows x64 build script
├── compile.sh                  # Linux build and AppImage script
└── packaging/                  # AppImage desktop entry and icon
```

## Build and run on Windows

Requirements:

- Windows 10 or Windows 11, x64
- Visual Studio 2022 or newer (including Visual Studio 2026), with **Desktop development with C++** and MSVC x64/x86 tools
- Windows 10 SDK (Direct3D 11, DXGI and D3DCompiler headers/libraries)
- CMake 3.20 or newer
- Your own legally obtained ROM copies

Place the ROM files here:

```text
GameData/OracleOfAges.gbc
GameData/OracleOfSeasons.gbc
```

Run `compile.bat`. It locates CMake and the MSVC x64/NMake toolchain, builds the recompiler and all test executables, runs CTest, generates bank-aware native C++ from both ROMs, then builds the Windows D3D11 application at `dist/Oracles.exe`. The script supports parent paths containing parentheses, e.g. `Oracles-PC-Recomp(1)`, and applies MSVC `/bigobj` only to the large generated game translation units.

Then launch:

```text
dist/Oracles.exe
```

The app opens a pre-game launcher. Before starting, choose:

- **Game:** Oracle of Ages or Oracle of Seasons.
- **Graphics adapter:** Automatic or one of the detected DXGI hardware adapters. Use Refresh to re-enumerate GPUs.
- **Display ratio:** 4:3 or 16:9 widescreen. The 160×144 game image is kept in its correct proportions inside the selected display canvas.
- **Window mode:** Windowed or borderless fullscreen.
- **Frame limit:** 40, 50, 60, 72, 75, 90, 100, 120, 144, 165, 180, 200, 240, 300, 360, 480, 600 FPS, or Uncapped. This limits presentation and does not change Game Boy CPU/PPU timing.
- **XInput controller:** Auto-detect the first connected controller or select XInput slots 1–4.
- **Keyboard bindings:** Click any binding button, then press the replacement key. If the key is already assigned to another action, the two bindings are swapped to avoid duplicates. Escape cancels a pending rebind.

Click **LAUNCH!** to initialize the cartridge and renderer. Once initialization succeeds, the launcher closes and the separate game window opens. The game window receives keyboard focus immediately; there is no need to click inside a viewport before playing. Settings are saved in `dist/config/settings.ini`.

Keyboard input is sampled from the foreground game window using both Win32 key messages and the current virtual-key state. The Game Boy P1 register uses the correct active-low row selectors: bit 5 selects A/B/Select/Start and bit 4 selects the D-pad. Runtime tests cover both selected rows, unselected-row isolation, simultaneous selection and joypad-interrupt falling edges. The selected XInput slot is polled independently of keyboard input and saved in launcher settings.

Keyboard input is sampled from the foreground game window using both Win32 key messages and the current virtual-key state. The Game Boy P1 register uses the hardware row selects correctly: bit 5 selects action buttons and bit 4 selects the D-pad. The input test suite checks each selected row, verifies that an unselected row cannot leak into reads, and checks joypad-interrupt falling edges.

### Default controls

| Game Boy control | Default keyboard | XInput controller |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| A | Z | A |
| B | X | B |
| Start | Enter | Start |
| Select | Right Shift | Back |

All eight keyboard actions can be rebound in the launcher. The game runs in a dedicated top-level window, so keyboard messages are handled by the game window rather than by launcher controls. Pressed-key state is cleared when the game loses focus to prevent stuck buttons. The implementation processes Win32 key-down/key-up messages and disambiguates left/right modifier keys using their scan-code flags. Microsoft documents that keyboard messages are delivered to the window with keyboard focus and that `SetFocus` establishes that recipient. <https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input>

Expected output:

```text
dist/
├── Oracles.exe
├── GameData/
│   ├── OracleOfAges.gbc
│   ├── OracleOfSeasons.gbc
│   ├── OracleOfAges.orcmeta
│   └── OracleOfSeasons.orcmeta
└── config/
    └── settings.ini        # created by the frontend
```

## Build on Linux and create an AppImage

Requirements: CMake 3.20+, a C++20 compiler, and your own `GameData/OracleOfAges.gbc` and `GameData/OracleOfSeasons.gbc` files. AppImage packaging also requires the official `linuxdeploy` and `appimagetool` x86_64 AppImages. Download them from the AppImage projects, make both tools executable, and either put them on `PATH` or set `LINUXDEPLOY` and `APPIMAGETOOL` to their paths.

Run:

```bash
bash compile.sh
```

The script builds the portable compiler, builds and runs CTest, generates native code from both supplied cartridges, builds `dist/Oracles`, and creates `dist/Oracles-PC-Recomp-v0.2.4-x86_64.AppImage`. The AppImage contains the application and bundled non-system dependencies, but **does not contain commercial ROMs**. Keep a `GameData/` folder beside the AppImage (or pass `--data-dir`).

The current Linux target is the project's portable headless frontend, not the Windows graphical launcher. It writes PPM frames and can export audio:

```bash
./dist/Oracles-PC-Recomp-v0.2.4-x86_64.AppImage --game ages --frames 2 --data-dir GameData --output-dir frames --audio-wav ages.wav
```

Official packaging references: [linuxdeploy user guide](https://docs.appimage.org/packaging-guide/from-source/linuxdeploy-user-guide.html) and [appimagetool usage](https://github.com/AppImage/appimagetool).

## Live execution and rendering path

The Windows frontend and portable integration test host use the same `GameRunner` implementation:

1. `Runtime::load_rom` loads a user-provided cartridge and initializes the CPU, memory map, cartridge state and CGB/DMG PPU state.
2. The selected generated Ages or Seasons dispatcher is called using the current PC and MBC5 ROM bank. Discovered basic blocks execute as compiled native C++.
3. If a program counter or banked target was not statically discovered, `GameRunner` executes that instruction with the project's own SM83 interpreter. This is a deliberate runtime fallback, not an external emulator.
4. Each generated instruction advances its own cycle count. Generated blocks yield at instruction boundaries when a PPU frame is ready, the CPU enters HALT/STOP, or an enabled interrupt needs servicing. This prevents a long native block from running past a frame-presentation or interrupt boundary.
5. The PPU renders a 160×144 RGBA framebuffer. On VBlank, the Windows frontend uploads that framebuffer to a D3D11 texture and presents it through the selected DXGI adapter and HWND swap chain.

There is no synthetic diagnostic image in the gameplay path. The D3D11 texture is initialized black until the first complete frame has been rendered by the loaded cartridge.

The portable `Oracles` target on non-Windows hosts uses the same `GameRunner` and generated native blocks, then writes actual PPU framebuffer images as PPM files. Example:

```text
Oracles --game ages --frames 2 --data-dir GameData --output-dir frames --audio-wav ages.wav
```

This headless path is useful for inspecting rendered frames and debugging the CPU/PPU connection; the Windows GUI is the normal interactive build.

## Implemented components

### Recompiler and cartridge runtime

- ROM header parser and 9-bit MBC5 ROM-bank selection, MBC5 RAM enable/bank behavior, ROM address-line mirroring and cartridge-sized external RAM mapping.
- SM83 base and CB-prefixed instruction decoder.
- Conservative bank-aware basic-block discovery and native C++ function emission.
- Generated native-block dispatch plus the project's own instruction interpreter for undiscovered/dynamic code targets.
- Per-instruction deferred cycle advancement, including path-dependent conditional JR/JP/CALL/RET timing.
- CPU interrupt dispatch, HALT/STOP handling, timer/divider behavior, CGB double-speed PPU clock scaling and serial-link behavior when no cable peer is attached.
- CGB `FF51`–`FF55` VRAM DMA register handling: masked source/destination addresses, GDMA complete transfers, HBlank HDMA 16-byte blocks, VBK selection, transfer status/cancellation and CPU stall cycles.
- Four-channel APU sample synthesis for square, wave and noise output, with basic volume envelopes and NR50/NR51 stereo mixing. Windows sends PCM to WaveOut; the portable CLI can export sound to a stereo 16-bit WAV file.
- Native block safe points for frame presentation and interrupt servicing.

### CGB/DMG PPU

- 160×144 RGBA framebuffer and VBlank frame-ready signalling.
- BG and Window tile-map rendering, unsigned/signed tile addressing, CGB tile attributes, VRAM bank selection, palette selection, flipping and priority.
- 8×8 and 8×16 OBJ rendering, 10-OBJ-per-scanline selection, palette/bank attributes, transparency, flipping and DMG/CGB priority ordering.
- LCDC/STAT/SCX/SCY/LY/LYC/BGP/OBP/WY/WX plus CGB VBK, BGPI/BGPD, OBPI/OBPD and OPRI register handling.
- LCD mode transitions, VBlank/STAT interrupt requests, CPU VRAM/OAM restrictions and CGB palette-data access restrictions.
- Scanline timing with major scroll/window/sprite Mode 3 penalties.

### Windows graphics and input

- Separate Win32 pre-game launcher and gameplay window. The launcher offers Ages/Seasons selection, DXGI GPU selection and refresh, 4:3/16:9 output, windowed/borderless-fullscreen mode, editable keyboard bindings, and a Launch button that closes the launcher after game-window initialization succeeds.
- Dedicated game-window keyboard input, configurable Win32 virtual-key bindings, left/right modifier-key normalization from scan-code data, focus-loss key-state clearing, and selectable XInput controller polling (Auto or slots 1–4). The launcher also offers frame-presentation limits from 40 FPS through 360 FPS or Uncapped.
- DXGI adapter enumeration and explicit D3D11 device creation on the selected hardware adapter; Automatic delegates to Windows' default hardware adapter.
- D3D11 texture upload, HLSL textured-quad renderer, point sampling, DXGI HWND swap chain and presentation.
- 4:3 mode uses a centered 4:3 canvas while preserving the 160×144 native frame without distortion.
- Widescreen mode uses a 16:9 canvas while preserving the native frame's aspect ratio; it does not invent extra game-world pixels or stretch the game's graphics.

## Current limitations

This version is a real live rendering implementation, but the following work remains before it can be called a fully playable and accurate Oracle recompilation:

1. **Not yet validated against the two retail ROMs on Windows.** The user's previous build logs confirm successful analysis and generated-source compilation, but this development environment does not contain their ROM files or a Windows SDK/MSVC runtime, so I cannot claim that both games reach and render their title screens on their PC yet.
2. **Static block discovery is conservative and incomplete.** Undiscovered and dynamic targets are interpreted by the in-tree SM83 fallback, but code executed from RAM and all unusual mapper/control-flow cases still need validation.
3. **The PPU is scanline-oriented, not FIFO/dot-fetch exact.** It models 456-dot lines, 144 visible lines, VBlank, LCD modes and major Mode 3 penalties. Fine-grained fetcher/FIFO stalls, mid-scanline raster effects and some timing quirks are not cycle accurate.
4. **DMA still needs hardware validation.** CGB GDMA and HBlank HDMA are implemented at the register/block level: GDMA copies all blocks and stalls the CPU; HDMA copies one 16-byte block at each visible HBlank and stalls the CPU for that block. Bus contention, mid-transfer bank changes, destination overflow and all edge cases are not cycle-accurate. OAM DMA (`FF46`) currently copies immediately rather than modelling its full timed bus behavior.
5. **Audio is an initial synthesizer, not yet hardware-accurate.** Square, wave and noise channels, basic volume envelopes and NR50/NR51 stereo mixing are synthesized at 32768 Hz. Windows uses WaveOut playback. The portable Linux headless build can export PCM as a WAV file with `--audio-wav`. Channel 1 frequency sweep, analog filtering, exact envelope/trigger timing, and per-dot APU edge behavior still need validation.
6. **Battery-backed SRAM is mapped in memory but is not yet persisted to `.sav` files.** Power-cycle save persistence and save-file compatibility remain future work.
7. **Linked-game password/data flow and cross-game integration have not been validated.**
8. **The Windows D3D11 frontend has not been compiled on this Linux build host.** The portable GameRunner and native generated-code path are verified here, but the user's MSVC 2026 run remains necessary to verify the Windows UI, D3D device creation and selected physical GPU.
9. **Widescreen is a 16:9 presentation canvas, not a camera/FOV expansion.** The 160×144 image remains correctly proportioned inside that canvas.

## Verification performed for v0.2.3

- All five portable CTest suites pass in a Release build.
- All five portable CTest suites pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- Added direct checks for `NR52` APU power/channel status, trigger/DAC behavior and length-counter expiry.
- Added CGB GDMA tests for 16-byte block copying, VRAM bank selection, write-only register behavior, `FF55` completion status and CPU stall cycles.
- Added HBlank HDMA tests for one-block-per-visible-HBlank transfer, progress status and completion.
- Added tests for native-block yield conditions: completed PPU frame, HALT, STOP and enabled interrupts.
- Added keyboard-binding mask tests covering default key codes, no-input behavior, and combined D-pad/action bits.
- Added fast CGB serial transfer timing in double-speed mode.
- End-to-end synthetic cartridges for both game slots: ROM → generated native C++ blocks → GameRunner → PPU → completed RGBA frame → PPM output. Both fixtures ran 5,487 generated native-block calls with zero interpreter fallback and produced 160×144 PPM files. These synthetic fixtures verify the execution/rendering pipeline, not either commercial game.
- ZIP/archive integrity is checked when creating the release archive.

These checks verify the independently implemented execution/rendering pipeline. They do not establish retail-ROM boot compatibility or full gameplay. No Windows D3D11 test result is claimed in this Linux environment.

## v0.2.4 work-branch verification

The GitHub Actions Linux CI run for this branch built the portable compiler and all five test executables, then passed all five CTest tests. It also generated the source archive. This verifies the portable compiler/runtime tests and archive packaging only; it does **not** verify a Windows/MSVC GUI build, WaveOut device playback, the Linux AppImage packaging step, or boot compatibility with the two retail ROMs. Those still need local build and cartridge validation.

## Independence and game-data policy

- No other Oracle recompilation's source, generated C/C++, executable, submodule or vendored runtime is included.
- Other repositories and technical specifications listed in `docs/RESEARCH_NOTES.md` were consulted only as references; their implementation code is not a build dependency.
- No ROM, extracted graphics, game audio, game code or save data is distributed.
- Generated native source is produced locally from ROMs supplied by the user.

## Next engineering milestones

1. Verify the Windows D3D11 window and first real frames with the two retail ROMs; fix boot/runtime errors revealed by the PC.
2. Refine DMA edge cases and implement timed OAM DMA.
3. Refine APU sweep/envelope timing and audio filtering; implement persistent battery-backed SRAM.
4. Replace approximate scanline rendering with a dot/FIFO-oriented PPU and validate using hardware test ROMs.
5. Improve static code discovery, RAM execution and computed-jump handling.
6. Validate title screens, file select, overworld, dungeons, transitions, saves and linked-game behavior for both games.
