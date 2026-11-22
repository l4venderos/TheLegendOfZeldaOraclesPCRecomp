# Changelog

## v0.2.4 — audio, presentation limits, controller selection and Linux packaging (work branch)

- Added an in-tree 32768 Hz stereo APU synthesizer for square-wave channels 1/2, wave channel 3 and noise channel 4, with basic envelope levels and NR50/NR51 mixing.
- Connected the Windows frontend to WaveOut PCM playback. Added buffered audio submission and shutdown handling.
- Added portable `--audio-wav` export to the headless application for generated 16-bit stereo PCM.
- Added launcher presentation-rate choices: 40, 60, 120, 144, 165, 240, 360 FPS and Uncapped. The Game Boy CPU/PPU scheduler remains at its native frame cadence, so higher limits do not overclock gameplay.
- Added XInput selection for Auto-detect or one of XInput slots 1–4; selection is stored in `config/settings.ini`.
- Added `compile.sh` and AppImage packaging metadata for the Linux x86_64 headless frontend. The build expects the official `linuxdeploy` and `appimagetool` utilities and does not include ROMs.
- Added an APU sample-generation regression test.

**Work-branch verification status:** source changes have been committed, but the v0.2.4 Windows/MSVC build, Linux/AppImage build and full CTest run have not yet been executed in this environment. Do not treat this work branch as a tested release. Audio sweep, analog filtering and hardware-accurate APU timing remain incomplete.

## v0.2.3 — Joypad input fix

- Corrected the P1/JOYP row-select mapping: bit 5 selects A/B/Select/Start; bit 4 selects Right/Left/Up/Down. The previous implementation had these selections reversed, so Oracle games polling one row at a time read the wrong host buttons.
- Added active-low P1 input-line calculation shared by reads, key-state updates and selection changes.
- Joypad interrupt requests now follow selected P1 high-to-low transitions; inputs in the unselected row do not spuriously request the interrupt.
- Added foreground-window `GetAsyncKeyState` sampling as a second input path alongside WM_KEYDOWN/WM_KEYUP, reducing missed taps when the synchronous frame runner temporarily delays window-message dispatch. It polls only while the game is the foreground window.
- Expanded runtime regression coverage for both rows, row isolation, simultaneous row selection and joypad interrupt edges.

## v0.2.2 — Launcher, keybindings and window modes

- Replaced the always-started combined control/game window with a separate pre-game launcher.
- Added game selection for Oracle of Ages and Oracle of Seasons before launch.
- Added pre-launch DXGI GPU selection and GPU list refresh.
- Added selectable 4:3 and 16:9 display canvases.
- Added windowed and borderless-fullscreen launch modes.
- Added editable keyboard bindings for D-pad, A, B, Start and Select. Bindings are saved in `config/settings.ini`.
- Launch now creates and initializes the game window first, then closes the launcher only after successful initialization.
- Fixed keyboard focus routing by giving the game its own top-level window with no launcher child controls. Key-down/up are processed on the active game window, cleared on focus loss, and right/left Shift/Ctrl/Alt are normalized from scan-code data.
- Added a portable input binding mapper and automated tests for Game Boy joypad bit layout.
- Added 4:3 aspect-fit regression coverage.

## v0.2.1

- Connected live GameRunner execution to the D3D11 framebuffer presenter.
- Added runtime peripheral and input integration improvements.
