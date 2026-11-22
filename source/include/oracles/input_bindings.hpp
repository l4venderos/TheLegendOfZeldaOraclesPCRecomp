#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oracles::input {

// Action order: Up, Down, Left, Right, A, B, Start, Select.
// Values are Win32 virtual-key codes, kept platform-neutral in this header.
inline constexpr std::array<unsigned, 8> kDefaultVirtualKeys{
    0x26u, 0x28u, 0x25u, 0x27u, 'Z', 'X', 0x0Du, 0xA1u
};

inline constexpr std::array<std::uint8_t, 8> kJoypadMasks{
    0x40u, 0x80u, 0x20u, 0x10u, 0x01u, 0x02u, 0x08u, 0x04u
};

inline constexpr std::uint8_t joypad_mask_from_actions(
    const std::array<bool, 8>& pressed) noexcept {
    std::uint8_t result = 0;
    for (std::size_t i = 0; i < pressed.size(); ++i) {
        if (pressed[i]) result = static_cast<std::uint8_t>(result | kJoypadMasks[i]);
    }
    return result;
}

} // namespace oracles::input
