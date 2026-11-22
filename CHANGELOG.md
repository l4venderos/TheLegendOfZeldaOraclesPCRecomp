# Changelog

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
