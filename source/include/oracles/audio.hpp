#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace oracles {

// Native Game Boy APU synthesizer. The runtime clocks this with CPU T-cycles;
// output is interleaved signed 16-bit stereo at 32768 Hz. Register state and
// channel enable flags remain owned by Runtime.
class AudioSynthesizer {
public:
    static constexpr unsigned kSampleRate = 32768;

    void reset() noexcept;
    void tick_cpu_cycle(const std::array<std::uint8_t, 0x80>& io,
                        bool powered, std::uint8_t active_channels,
                        bool double_speed);
    std::vector<std::int16_t> take_samples();

private:
    void synthesize(const std::array<std::uint8_t, 0x80>& io,
                    bool powered, std::uint8_t active_channels);
    std::uint32_t sample_clock_{};
    std::uint64_t sample_count_{};
    std::array<double, 3> phase_{};
    double noise_phase_{};
    std::uint16_t noise_lfsr_{0x7FFF};
    std::uint8_t previous_channels_{};
    std::array<std::uint8_t, 3> envelope_volume_{};
    std::array<std::uint16_t, 3> envelope_clock_{};
    std::vector<std::int16_t> samples_;
};

} // namespace oracles
