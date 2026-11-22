#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace oracles {

class Runtime;

// Scanline-oriented Game Boy Color PPU implementation.
// Timing models the four LCD modes and their major Mode 3 penalties. Pixel
// composition implements CGB BG/window attributes, CGB palettes and OBJs.
// It is deliberately not described as dot-FIFO/DMG-silicon-cycle accurate.
class Ppu {
public:
    static constexpr unsigned kScreenWidth = 160;
    static constexpr unsigned kScreenHeight = 144;

    Ppu();
    void reset(Runtime& runtime);
    void tick(Runtime& runtime, std::uint32_t dots);

    bool read_register(const Runtime& runtime, std::uint16_t address,
                       std::uint8_t& value) const;
    bool write_register(Runtime& runtime, std::uint16_t address,
                        std::uint8_t value);

    bool cpu_can_access_vram() const noexcept;
    bool cpu_can_access_oam() const noexcept;
    bool frame_ready() const noexcept { return frame_ready_; }
    bool consume_frame_ready() noexcept;
    const std::array<std::uint32_t, kScreenWidth * kScreenHeight>& framebuffer() const noexcept {
        return framebuffer_;
    }
    std::uint8_t mode() const noexcept { return mode_; }
    std::uint8_t ly() const noexcept { return ly_; }
    std::uint16_t dot() const noexcept { return dot_; }
    bool cgb_mode() const noexcept { return cgb_mode_; }

private:
    struct Sprite {
        std::uint8_t oam_index{};
        std::uint8_t y{};
        std::uint8_t x{};
        std::uint8_t tile{};
        std::uint8_t attributes{};
    };
    struct BgPixel {
        std::uint8_t color_index{};
        std::uint8_t attributes{};
        std::uint32_t rgba{};
    };
    struct ObjPixel {
        std::uint8_t color_index{};
        std::uint8_t attributes{};
        std::uint8_t palette{};
        std::uint32_t rgba{};
        bool present{};
    };

    std::uint16_t dot_{};
    std::uint16_t mode3_length_{172};
    std::uint8_t ly_{};
    std::uint8_t mode_{};
    std::uint8_t window_line_{};
    bool cgb_mode_{true};
    bool frame_ready_{};
    bool stat_irq_line_{};
    std::uint8_t bg_palette_index_{};
    std::uint8_t obj_palette_index_{};
    std::array<std::uint8_t, 64> bg_palette_ram_{};
    std::array<std::uint8_t, 64> obj_palette_ram_{};
    std::array<std::uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    std::vector<Sprite> line_sprites_;

    void set_mode(Runtime& runtime, std::uint8_t mode);
    void update_stat(Runtime& runtime);
    void begin_oam_scan(Runtime& runtime);
    std::uint16_t calculate_mode3_length(const Runtime& runtime) const;
    void render_scanline(const Runtime& runtime);
    BgPixel sample_bg(const Runtime& runtime, int x, int y, bool window) const;
    ObjPixel sample_obj(const Runtime& runtime, int x, int y) const;
    std::uint32_t bg_colour(const Runtime& runtime, std::uint8_t palette,
                            std::uint8_t colour_index) const;
    std::uint32_t obj_colour(const Runtime& runtime, std::uint8_t palette,
                             std::uint8_t colour_index) const;
    static std::uint32_t rgb555_to_rgba(std::uint16_t value);
    static std::uint32_t dmg_shade(std::uint8_t palette, std::uint8_t colour_index);
};

} // namespace oracles
