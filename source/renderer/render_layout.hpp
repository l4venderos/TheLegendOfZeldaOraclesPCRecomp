#pragma once

#include <algorithm>
#include <cstdint>

namespace oracles::render {

struct RectI {
    int x{};
    int y{};
    int width{};
    int height{};
};

// Returns the largest centered rectangle with the requested aspect ratio
// that fits inside the available client area. This is shared by the renderer
// and portable layout tests.
inline RectI fit_aspect(int available_width, int available_height,
                        std::uint32_t aspect_numerator,
                        std::uint32_t aspect_denominator) {
    if (available_width <= 0 || available_height <= 0 ||
        aspect_numerator == 0 || aspect_denominator == 0) {
        return {};
    }

    int width = available_width;
    int height = static_cast<int>(
        (static_cast<std::int64_t>(width) * aspect_denominator) /
        aspect_numerator);

    if (height > available_height) {
        height = available_height;
        width = static_cast<int>(
            (static_cast<std::int64_t>(height) * aspect_numerator) /
            aspect_denominator);
    }

    width = std::max(1, width);
    height = std::max(1, height);
    return {(available_width - width) / 2,
            (available_height - height) / 2,
            width, height};
}

} // namespace oracles::render
