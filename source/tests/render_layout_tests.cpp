#include "../renderer/render_layout.hpp"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using oracles::render::fit_aspect;

    const auto original = fit_aspect(800, 600, 10, 9);
    require(original.width == 666 && original.height == 600,
            "10:9 original canvas must fit without distortion");
    require(original.x == 67 && original.y == 0,
            "10:9 canvas must be horizontally centered");

    const auto standard = fit_aspect(1280, 720, 4, 3);
    require(standard.width == 960 && standard.height == 720,
            "4:3 canvas should fit a 16:9 target without distortion");
    require(standard.x == 160 && standard.y == 0,
            "4:3 canvas must be horizontally centered in a widescreen target");
    const auto standard_image = fit_aspect(standard.width, standard.height, 10, 9);
    require(standard_image.width == 800 && standard_image.height == 720 &&
            standard_image.x == 80 && standard_image.y == 0,
            "native 10:9 image must be centered without distortion inside 4:3 canvas");

    const auto wide = fit_aspect(1280, 720, 16, 9);
    require(wide.width == 1280 && wide.height == 720,
            "16:9 canvas should fill a 16:9 target");

    const auto nested = fit_aspect(wide.width, wide.height, 10, 9);
    require(nested.width == 800 && nested.height == 720,
            "native 10:9 frame must be preserved inside a 16:9 canvas");
    require(nested.x == 240 && nested.y == 0,
            "native frame must be centered inside the widescreen canvas");

    const auto small = fit_aspect(320, 180, 256, 144);
    require(small.width == 320 && small.height == 180,
            "expanded 256x144 CGB canvas must occupy widescreen viewport");

    const auto invalid = fit_aspect(0, 180, 16, 9);
    require(invalid.width == 0 && invalid.height == 0,
            "zero-sized targets must return an empty rectangle");

    std::cout << "Render layout tests passed.\n";
    return 0;
}
