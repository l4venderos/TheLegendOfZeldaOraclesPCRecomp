# Keyboard and Game Boy joypad input

## Host keyboard path

The Windows game window accepts `WM_KEYDOWN` and `WM_KEYUP` messages, normalizes left/right modifier keys using the scan-code and extended-key fields, and maps configured virtual-key codes into the eight host button bits. The timer also samples `GetAsyncKeyState` for the configured keys, but only when the game window is the foreground window. The high-order bit is used for current down/up state; the least-significant “pressed since previous call” bit is deliberately ignored because Microsoft documents it as unreliable.

References:

- Microsoft, [Keyboard Input Overview](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input): keyboard messages are delivered to the window with keyboard focus; `WM_KEYDOWN`/`WM_KEYUP` describe key transitions.
- Microsoft, [GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate): the most significant bit reports whether the key is currently down.

## Game Boy P1/JOYP mapping

`FF00` is a 2×4 active-low input matrix. The row select bits are:

- Bit 5 / P15: clear to select A, B, Select and Start.
- Bit 4 / P14: clear to select Right, Left, Up and Down.
- Bits 0–3: read-only active-low lines; a pressed selected button clears its corresponding bit.

If both rows are selected, the input lines are effectively combined (a bit is low if a matching key in either selected row is pressed). The Joypad interrupt is requested when any selected P1 bit 0–3 transitions from high to low. The runtime computes the active-low nibble once and uses the same helper for reads, host-button updates and row-selection changes.

References:

- [Pan Docs — Joypad Input](https://gbdev.io/pandocs/Joypad_Input.html)
- [Pan Docs — Interrupt Sources, Joypad interrupt](https://gbdev.io/pandocs/Interrupt_Sources.html#int-60--joypad-interrupt)
- [mGBA Game Boy Documentation — FF00 P1/JOYP](https://mgba-emu.github.io/gbdoc/)

## Regression coverage

The runtime test suite checks action-row selection, direction-row selection, isolation of the unselected row, simultaneous row selection, and interrupt edges caused both by a button press and by selecting a row containing an already-held button.
