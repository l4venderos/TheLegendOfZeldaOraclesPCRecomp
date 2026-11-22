#include "oracles/ppu.hpp"
#include "oracles/runtime.hpp"

#include <algorithm>
#include <array>

namespace oracles {
namespace {
constexpr std::uint16_t kDotsPerLine = 456;
constexpr std::uint8_t kModeHBlank = 0;
constexpr std::uint8_t kModeVBlank = 1;
constexpr std::uint8_t kModeOam = 2;
constexpr std::uint8_t kModeTransfer = 3;
constexpr std::uint8_t kInterruptVBlank = 0x01;
constexpr std::uint8_t kInterruptStat = 0x02;

std::uint32_t make_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return 0xFF000000u | (std::uint32_t(b) << 16) |
           (std::uint32_t(g) << 8) | std::uint32_t(r);
}

std::uint8_t tile_pixel(const Runtime& runtime, unsigned bank,
                        int tile_index, unsigned row, unsigned column) {
    const unsigned offset = (bank & 1u) * 0x2000u +
        static_cast<unsigned>(tile_index) * 16u + row * 2u;
    if (offset + 1 >= runtime.vram.size()) return 0;
    const std::uint8_t lo = runtime.vram[offset];
    const std::uint8_t hi = runtime.vram[offset + 1];
    const unsigned bit = 7u - (column & 7u);
    return static_cast<std::uint8_t>(((lo >> bit) & 1u) | (((hi >> bit) & 1u) << 1));
}

bool sprite_covers_line(std::uint8_t raw_y, unsigned height, unsigned line) {
    const int top = static_cast<int>(raw_y) - 16;
    return static_cast<int>(line) >= top && static_cast<int>(line) < top + static_cast<int>(height);
}
}

Ppu::Ppu() {
    framebuffer_.fill(make_rgba(255, 255, 255));
}

void Ppu::reset(Runtime& runtime) {
    dot_ = 0;
    mode3_length_ = 172;
    ly_ = 0;
    mode_ = 2;
    window_line_ = 0;
    cgb_mode_ = runtime.rom.size() > 0x143 && (runtime.rom[0x143] & 0x80) != 0;
    frame_ready_ = false;
    stat_irq_line_ = false;
    bg_palette_index_ = 0;
    obj_palette_index_ = 0;
    line_sprites_.clear();

    // Deterministic fallback until the cartridge initializes CGB palette RAM.
    // Hardware power-up palette contents are not a stable software contract.
    constexpr std::array<std::uint16_t, 4> defaults = {0x7FFF, 0x56B5, 0x2D6B, 0x0000};
    for (unsigned palette = 0; palette < 8; ++palette) {
        for (unsigned colour = 0; colour < 4; ++colour) {
            const auto value = defaults[colour];
            const unsigned offset = palette * 8 + colour * 2;
            bg_palette_ram_[offset] = static_cast<std::uint8_t>(value & 0xFF);
            bg_palette_ram_[offset + 1] = static_cast<std::uint8_t>(value >> 8);
            obj_palette_ram_[offset] = static_cast<std::uint8_t>(value & 0xFF);
            obj_palette_ram_[offset + 1] = static_cast<std::uint8_t>(value >> 8);
        }
    }

    runtime.io[0x40] = 0x91;
    runtime.io[0x41] = 0x80;
    runtime.io[0x44] = 0x00;
    runtime.io[0x47] = 0xFC;
    runtime.io[0x48] = 0xFF;
    runtime.io[0x49] = 0xFF;
    runtime.io[0x4F] = 0xFE;
    runtime.io[0x68] = 0x00;
    runtime.io[0x6A] = 0x00;
    runtime.io[0x6C] = 0xFE;
    framebuffer_.fill(make_rgba(255, 255, 255));
    update_stat(runtime);
}

bool Ppu::cpu_can_access_vram() const noexcept {
    return mode_ != kModeTransfer;
}

bool Ppu::cpu_can_access_oam() const noexcept {
    return mode_ != kModeOam && mode_ != kModeTransfer;
}

bool Ppu::consume_frame_ready() noexcept {
    const bool ready = frame_ready_;
    frame_ready_ = false;
    return ready;
}

bool Ppu::read_register(const Runtime& runtime, std::uint16_t address,
                        std::uint8_t& value) const {
    if (address < 0xFF40 || address > 0xFF6C) return false;
    switch (address) {
    case 0xFF40: case 0xFF42: case 0xFF43: case 0xFF45:
    case 0xFF47: case 0xFF48: case 0xFF49: case 0xFF4A: case 0xFF4B:
        value = runtime.io[address - 0xFF00];
        return true;
    case 0xFF41:
        value = static_cast<std::uint8_t>(0x80 | (runtime.io[0x41] & 0x78) |
            ((ly_ == runtime.io[0x45]) ? 0x04 : 0x00) | (mode_ & 0x03));
        return true;
    case 0xFF44:
        value = ly_;
        return true;
    case 0xFF4F:
        value = cgb_mode_ ? static_cast<std::uint8_t>(0xFE | (runtime.vram_bank & 1)) : 0xFF;
        return true;
    case 0xFF68:
        value = cgb_mode_ ? bg_palette_index_ : 0xFF;
        return true;
    case 0xFF69:
        value = (!cgb_mode_ || mode_ == kModeTransfer) ? 0xFF
            : bg_palette_ram_[bg_palette_index_ & 0x3F];
        return true;
    case 0xFF6A:
        value = cgb_mode_ ? obj_palette_index_ : 0xFF;
        return true;
    case 0xFF6B:
        value = (!cgb_mode_ || mode_ == kModeTransfer) ? 0xFF
            : obj_palette_ram_[obj_palette_index_ & 0x3F];
        return true;
    case 0xFF6C:
        value = cgb_mode_ ? static_cast<std::uint8_t>(0xFE | (runtime.io[0x6C] & 1)) : 0xFF;
        return true;
    default:
        return false;
    }
}

bool Ppu::write_register(Runtime& runtime, std::uint16_t address, std::uint8_t value) {
    if (address < 0xFF40 || address > 0xFF6C) return false;
    switch (address) {
    case 0xFF40: {
        const bool was_enabled = (runtime.io[0x40] & 0x80) != 0;
        const bool now_enabled = (value & 0x80) != 0;
        runtime.io[0x40] = value;
        if (was_enabled && !now_enabled) {
            dot_ = 0; ly_ = 0; mode_ = kModeHBlank; mode3_length_ = 172;
            window_line_ = 0; line_sprites_.clear();
            framebuffer_.fill(make_rgba(255, 255, 255));
            // LCD shutdown clears internal display state but does not mean a
            // complete 160x144 frame was scanned out. The frontend must wait
            // for the next VBlank after LCD is enabled again.
            frame_ready_ = false;
            update_stat(runtime);
        } else if (!was_enabled && now_enabled) {
            dot_ = 0; ly_ = 0; mode_ = kModeOam; window_line_ = 0;
            line_sprites_.clear();
            update_stat(runtime);
        }
        return true;
    }
    case 0xFF41:
        runtime.io[0x41] = static_cast<std::uint8_t>((runtime.io[0x41] & 0x07) | (value & 0x78) | 0x80);
        update_stat(runtime);
        return true;
    case 0xFF42: case 0xFF43: case 0xFF45:
    case 0xFF47: case 0xFF48: case 0xFF49: case 0xFF4A: case 0xFF4B:
        runtime.io[address - 0xFF00] = value;
        if (address == 0xFF45) update_stat(runtime);
        return true;
    case 0xFF44:
        return true; // LY is read-only.
    case 0xFF4F:
        if (cgb_mode_) {
            runtime.vram_bank = value & 1;
            runtime.io[0x4F] = static_cast<std::uint8_t>(0xFE | runtime.vram_bank);
        }
        return true;
    case 0xFF68:
        if (cgb_mode_) {
            bg_palette_index_ = value & 0xBF;
            runtime.io[0x68] = bg_palette_index_;
        }
        return true;
    case 0xFF69:
        if (cgb_mode_) {
            if (mode_ != kModeTransfer) bg_palette_ram_[bg_palette_index_ & 0x3F] = value;
            // Pan Docs specifies that auto-increment occurs even if a Mode 3 write fails.
            if (bg_palette_index_ & 0x80) {
                bg_palette_index_ = static_cast<std::uint8_t>(0x80 | ((bg_palette_index_ + 1) & 0x3F));
                runtime.io[0x68] = bg_palette_index_;
            }
        }
        return true;
    case 0xFF6A:
        if (cgb_mode_) {
            obj_palette_index_ = value & 0xBF;
            runtime.io[0x6A] = obj_palette_index_;
        }
        return true;
    case 0xFF6B:
        if (cgb_mode_) {
            if (mode_ != kModeTransfer) obj_palette_ram_[obj_palette_index_ & 0x3F] = value;
            if (obj_palette_index_ & 0x80) {
                obj_palette_index_ = static_cast<std::uint8_t>(0x80 | ((obj_palette_index_ + 1) & 0x3F));
                runtime.io[0x6A] = obj_palette_index_;
            }
        }
        return true;
    case 0xFF6C:
        if (cgb_mode_) runtime.io[0x6C] = value & 1;
        return true;
    default:
        return false;
    }
}

void Ppu::update_stat(Runtime& runtime) {
    if (!(runtime.io[0x40] & 0x80)) { mode_ = kModeHBlank; ly_ = 0; }
    const bool coincidence = ly_ == runtime.io[0x45];
    const std::uint8_t enables = runtime.io[0x41] & 0x78;
    const bool irq_line = (mode_ == 0 && (enables & 0x08)) ||
        (mode_ == 1 && (enables & 0x10)) || (mode_ == 2 && (enables & 0x20)) ||
        (coincidence && (enables & 0x40));
    runtime.io[0x41] = static_cast<std::uint8_t>(0x80 | enables |
        (coincidence ? 0x04 : 0) | (mode_ & 3));
    runtime.io[0x44] = ly_;
    if (irq_line && !stat_irq_line_) runtime.io[0x0F] |= kInterruptStat;
    stat_irq_line_ = irq_line;
}

void Ppu::set_mode(Runtime& runtime, std::uint8_t mode) {
    mode_ = mode;
    update_stat(runtime);
    if (mode == kModeHBlank && ly_ < kScreenHeight) runtime.on_hblank();
}

void Ppu::tick(Runtime& runtime, std::uint32_t dots) {
    if (!(runtime.io[0x40] & 0x80) || dots == 0) return;
    while (dots) {
        std::uint16_t boundary = kDotsPerLine;
        if (mode_ == kModeOam) boundary = 80;
        else if (mode_ == kModeTransfer) boundary = static_cast<std::uint16_t>(80 + mode3_length_);
        const auto remaining = static_cast<std::uint16_t>(boundary - dot_);
        const auto step = static_cast<std::uint16_t>(std::min<std::uint32_t>(dots, remaining));
        dot_ = static_cast<std::uint16_t>(dot_ + step);
        dots -= step;

        if (dot_ != boundary) continue;
        if (mode_ == kModeOam && dot_ == 80) {
            begin_oam_scan(runtime);
            mode3_length_ = calculate_mode3_length(runtime);
            set_mode(runtime, kModeTransfer);
        } else if (mode_ == kModeTransfer && dot_ == 80 + mode3_length_) {
            render_scanline(runtime);
            set_mode(runtime, kModeHBlank);
        } else if (dot_ == kDotsPerLine) {
            dot_ = 0;
            ++ly_;
            if (ly_ == 144) {
                set_mode(runtime, kModeVBlank);
                runtime.io[0x0F] |= kInterruptVBlank;
                frame_ready_ = true;
            } else if (ly_ > 153) {
                ly_ = 0;
                window_line_ = 0;
                line_sprites_.clear();
                set_mode(runtime, kModeOam);
            } else if (ly_ < 144) {
                line_sprites_.clear();
                set_mode(runtime, kModeOam);
            } else {
                set_mode(runtime, kModeVBlank);
            }
        }
    }
}

void Ppu::begin_oam_scan(Runtime& runtime) {
    line_sprites_.clear();
    const unsigned height = (runtime.io[0x40] & 0x04) ? 16u : 8u;
    for (unsigned index = 0; index < 40 && line_sprites_.size() < 10; ++index) {
        const unsigned offset = index * 4;
        const auto y = runtime.oam[offset];
        if (!sprite_covers_line(y, height, ly_)) continue;
        line_sprites_.push_back({static_cast<std::uint8_t>(index), y,
            runtime.oam[offset + 1], runtime.oam[offset + 2], runtime.oam[offset + 3]});
    }
    // DMG priority is smaller X first, then lower OAM index. Native CGB mode
    // normally keeps OAM order; OPRI's DMG-style ordering is retained for
    // software-controlled compatibility configurations.
    if (!cgb_mode_ || (runtime.io[0x6C] & 1)) {
        std::stable_sort(line_sprites_.begin(), line_sprites_.end(),
            [](const Sprite& a, const Sprite& b) {
                if (a.x != b.x) return a.x < b.x;
                return a.oam_index < b.oam_index;
            });
    }
}

std::uint16_t Ppu::calculate_mode3_length(const Runtime& runtime) const {
    const auto lcdc = runtime.io[0x40];
    unsigned length = 172u + (runtime.io[0x43] & 7u);
    const bool window_visible = (lcdc & 0x20) && ly_ >= runtime.io[0x4A] &&
        runtime.io[0x4B] <= 166 && (cgb_mode_ || (lcdc & 0x01));
    if (window_visible) length += 6;
    if (lcdc & 0x02) {
        const int window_start = static_cast<int>(runtime.io[0x4B]) - 7;
        for (const auto& sprite : line_sprites_) {
            if (sprite.x == 0) { length += 11; continue; }
            const int left = static_cast<int>(sprite.x) - 8;
            const int pixel_x = (window_visible && left >= window_start)
                ? left - window_start : left + runtime.io[0x43];
            const int within = ((pixel_x % 8) + 8) % 8;
            // Every selected object can incur a fetch penalty, including
            // multiple objects that overlap the same tile column.
            length += static_cast<unsigned>(6 + std::max(0, 5 - within));
        }
    }
    return static_cast<std::uint16_t>(std::min(289u, length));
}

std::uint32_t Ppu::rgb555_to_rgba(std::uint16_t value) {
    const auto r5 = static_cast<std::uint8_t>(value & 0x1F);
    const auto g5 = static_cast<std::uint8_t>((value >> 5) & 0x1F);
    const auto b5 = static_cast<std::uint8_t>((value >> 10) & 0x1F);
    return make_rgba(static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
                     static_cast<std::uint8_t>((g5 << 3) | (g5 >> 2)),
                     static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2)));
}

std::uint32_t Ppu::dmg_shade(std::uint8_t palette, std::uint8_t colour_index) {
    static constexpr std::array<std::uint8_t, 4> shades = {255, 192, 96, 0};
    const auto mapped = static_cast<unsigned>((palette >> ((colour_index & 3u) * 2u)) & 3u);
    return make_rgba(shades[mapped], shades[mapped], shades[mapped]);
}

std::uint32_t Ppu::bg_colour(const Runtime& runtime, std::uint8_t palette,
                             std::uint8_t colour_index) const {
    if (!cgb_mode_) return dmg_shade(runtime.io[0x47], colour_index);
    const unsigned offset = (unsigned(palette & 7) * 8) + (unsigned(colour_index & 3) * 2);
    const std::uint16_t colour = static_cast<std::uint16_t>(bg_palette_ram_[offset] |
        (std::uint16_t(bg_palette_ram_[offset + 1]) << 8));
    return rgb555_to_rgba(colour);
}

std::uint32_t Ppu::obj_colour(const Runtime& runtime, std::uint8_t palette,
                             std::uint8_t colour_index) const {
    if (!cgb_mode_) {
        return dmg_shade(runtime.io[(palette & 1) ? 0x49 : 0x48], colour_index);
    }
    const unsigned offset = (unsigned(palette & 7) * 8) + (unsigned(colour_index & 3) * 2);
    const std::uint16_t colour = static_cast<std::uint16_t>(obj_palette_ram_[offset] |
        (std::uint16_t(obj_palette_ram_[offset + 1]) << 8));
    return rgb555_to_rgba(colour);
}

Ppu::BgPixel Ppu::sample_bg(const Runtime& runtime, int x, int y, bool window) const {
    const auto lcdc = runtime.io[0x40];
    if (!cgb_mode_ && !(lcdc & 0x01)) {
        // In DMG mode, clearing LCDC.0 blanks BG and Window to white; OBJ
        // pixels may still be composed over this colour.
        return {0, 0, make_rgba(255, 255, 255)};
    }
    const unsigned map_base = window
        ? ((lcdc & 0x40) ? 0x1C00u : 0x1800u)
        : ((lcdc & 0x08) ? 0x1C00u : 0x1800u);
    const unsigned map_x = static_cast<unsigned>(x) & 0xFFu;
    const unsigned map_y = static_cast<unsigned>(y) & 0xFFu;
    const unsigned map_offset = map_base + ((map_y >> 3) * 32u) + (map_x >> 3);
    const std::uint8_t tile_id = runtime.vram[map_offset];
    const std::uint8_t attributes = cgb_mode_ ? runtime.vram[0x2000u + map_offset] : 0;
    unsigned row = map_y & 7u;
    unsigned column = map_x & 7u;
    if (attributes & 0x40) row = 7u - row;
    if (attributes & 0x20) column = 7u - column;
    const int signed_tile = tile_id < 0x80 ? int(tile_id) : int(tile_id) - 0x100;
    const int tile_index = (lcdc & 0x10) ? int(tile_id) : (256 + signed_tile);
    const auto colour_index = tile_pixel(runtime, (attributes >> 3) & 1u,
                                         tile_index, row, column);
    return {colour_index, attributes,
        bg_colour(runtime, cgb_mode_ ? (attributes & 7u) : 0, colour_index)};
}

Ppu::ObjPixel Ppu::sample_obj(const Runtime& runtime, int x, int y) const {
    ObjPixel result{};
    if (!(runtime.io[0x40] & 0x02)) return result;
    const unsigned height = (runtime.io[0x40] & 0x04) ? 16u : 8u;
    for (const auto& sprite : line_sprites_) {
        const int left = static_cast<int>(sprite.x) - 8;
        const int top = static_cast<int>(sprite.y) - 16;
        if (x < left || x >= left + 8 || y < top || y >= top + static_cast<int>(height)) continue;
        unsigned row = static_cast<unsigned>(y - top);
        unsigned column = static_cast<unsigned>(x - left);
        if (sprite.attributes & 0x40) row = height - 1u - row;
        if (sprite.attributes & 0x20) column = 7u - column;
        unsigned tile = sprite.tile;
        if (height == 16) { tile &= 0xFEu; tile += row / 8u; row &= 7u; }
        const unsigned bank = cgb_mode_ ? ((sprite.attributes >> 3) & 1u) : 0u;
        const auto colour_index = tile_pixel(runtime, bank, static_cast<int>(tile), row, column);
        if (colour_index == 0) continue;
        result.color_index = colour_index;
        result.attributes = sprite.attributes;
        result.palette = cgb_mode_ ? (sprite.attributes & 7u) : ((sprite.attributes >> 4) & 1u);
        result.rgba = obj_colour(runtime, result.palette, colour_index);
        result.present = true;
        break; // OBJ-vs-OBJ priority is resolved before BG-vs-OBJ priority.
    }
    return result;
}

void Ppu::render_scanline(const Runtime& runtime) {
    if (ly_ >= kScreenHeight) return;
    const auto lcdc = runtime.io[0x40];
    const bool window_enabled = (lcdc & 0x20) && ly_ >= runtime.io[0x4A] &&
        runtime.io[0x4B] <= 166 && (cgb_mode_ || (lcdc & 0x01));
    const int window_start = static_cast<int>(runtime.io[0x4B]) - 7;
    bool drew_window = false;

    for (int x = 0; x < static_cast<int>(kScreenWidth); ++x) {
        const bool use_window = window_enabled && x >= window_start;
        BgPixel bg{};
        if (use_window) {
            bg = sample_bg(runtime, x - window_start, window_line_, true);
            drew_window = true;
        } else {
            bg = sample_bg(runtime, (x + runtime.io[0x43]) & 0xFF,
                           (ly_ + runtime.io[0x42]) & 0xFF, false);
        }

        const ObjPixel obj = sample_obj(runtime, x, ly_);
        std::uint32_t output = bg.rgba;
        if (obj.present) {
            bool bg_has_priority = false;
            if (cgb_mode_) {
                bg_has_priority = bg.color_index != 0 && (lcdc & 0x01) &&
                    ((bg.attributes & 0x80) || (obj.attributes & 0x80));
            } else {
                bg_has_priority = bg.color_index != 0 && (obj.attributes & 0x80);
            }
            if (!bg_has_priority) output = obj.rgba;
        }
        framebuffer_[static_cast<std::size_t>(ly_) * kScreenWidth + static_cast<unsigned>(x)] = output;
    }
    if (drew_window && window_line_ < 144) ++window_line_;
}

} // namespace oracles
