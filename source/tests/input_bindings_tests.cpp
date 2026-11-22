#include "oracles/input_bindings.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    using oracles::input::joypad_mask_from_actions;
    std::array<bool, 8> keys{};

    if (joypad_mask_from_actions(keys) != 0) {
        std::cerr << "FAIL: no keys must produce an unpressed joypad mask\n";
        return 1;
    }

    keys[0] = true; // Up
    keys[4] = true; // A
    keys[6] = true; // Start
    if (joypad_mask_from_actions(keys) != 0x49u) {
        std::cerr << "FAIL: Up + A + Start must map to 0x49\n";
        return 1;
    }

    keys[0] = false;
    keys[4] = false;
    keys[6] = false;
    keys[1] = keys[2] = keys[3] = keys[5] = keys[7] = true;
    if (joypad_mask_from_actions(keys) != 0xB6u) {
        std::cerr << "FAIL: Down + Left + Right + B + Select must map to 0xB6\n";
        return 1;
    }

    if (oracles::input::kDefaultVirtualKeys[0] != 0x26u ||
        oracles::input::kDefaultVirtualKeys[4] != 'Z' ||
        oracles::input::kDefaultVirtualKeys[7] != 0xA1u) {
        std::cerr << "FAIL: default keyboard bindings changed unexpectedly\n";
        return 1;
    }

    std::cout << "Keyboard binding tests passed.\n";
    return 0;
}
