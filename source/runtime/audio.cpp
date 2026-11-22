#include "oracles/audio.hpp"

#include <algorithm>
#include <cmath>

namespace oracles {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr std::array<double, 4> kDutyThreshold{0.125, 0.25, 0.5, 0.75};

double pulse_sample(double& phase, std::uint16_t frequency,
                    unsigned duty, std::uint8_t volume) {
    if (frequency >= 2048 || volume == 0) return 0.0;
    const double hz = 131072.0 / static_cast<double>(2048u - frequency);
    phase += hz / AudioSynthesizer::kSampleRate;
    phase -= std::floor(phase);
    return (phase < kDutyThreshold[duty & 3u] ? 1.0 : -1.0) * volume / 15.0;
}
} // namespace

void AudioSynthesizer::reset() noexcept {
    sample_clock_ = 0;
    phase_.fill(0.0);
    noise_phase_ = 0.0;
    noise_lfsr_ = 0x7FFF;
    envelope_volume_.fill(0);
    envelope_clock_.fill(0);
    samples_generated_ = 0;
    samples_.clear();
}

void AudioSynthesizer::tick_cpu_cycle(
    const std::array<std::uint8_t, 0x80>& io,
    bool powered, std::uint8_t active_channels, bool double_speed) {
    // The APU remains clocked at the normal 4.194304 MHz base rate in CGB
    // double-speed mode, where each CPU T-cycle represents half a base tick.
    sample_clock_ += double_speed ? 1u : 2u;
    if (sample_clock_ >= 256u) {
        sample_clock_ -= 256u;
        synthesize(io, powered, active_channels);
    }
}

void AudioSynthesizer::synthesize(
    const std::array<std::uint8_t, 0x80>& io,
    bool powered, std::uint8_t active_channels) {
    ++samples_generated_;
    if (!powered) {
        samples_.push_back(0);
        samples_.push_back(0);
        return;
    }

    // Envelope clocks are 64 Hz. This is deliberately separated from the
    // host playback rate so changing the output buffer size does not change
    // the emulated sound envelope timing.
    if ((samples_generated_ & 511u) == 0) {
        constexpr std::array<unsigned, 3> env_regs{0x12, 0x17, 0x21};
        for (unsigned channel = 0; channel < env_regs.size(); ++channel) {
            const auto reg = io[env_regs[channel]];
            const unsigned period = reg & 7u;
            if (period == 0) continue;
            if (++envelope_clock_[channel] >= period) {
                envelope_clock_[channel] = 0;
                if (reg & 0x08u) {
                    if (envelope_volume_[channel] < 15) ++envelope_volume_[channel];
                } else if (envelope_volume_[channel] > 0) {
                    --envelope_volume_[channel];
                }
            }
        }
    }

    if ((active_channels & 0x01u) == 0) envelope_volume_[0] = io[0x12] >> 4;
    if ((active_channels & 0x02u) == 0) envelope_volume_[1] = io[0x17] >> 4;
    if ((active_channels & 0x08u) == 0) envelope_volume_[2] = io[0x21] >> 4;

    double channel[4]{};
    const auto f1 = static_cast<std::uint16_t>(io[0x13] | ((io[0x14] & 7u) << 8));
    const auto f2 = static_cast<std::uint16_t>(io[0x18] | ((io[0x19] & 7u) << 8));
    if ((active_channels & 0x01u) && (io[0x12] & 0xF8u))
        channel[0] = pulse_sample(phase_[0], f1, io[0x11] >> 6, envelope_volume_[0]);
    if ((active_channels & 0x02u) && (io[0x17] & 0xF8u))
        channel[1] = pulse_sample(phase_[1], f2, io[0x16] >> 6, envelope_volume_[1]);

    // Channel 3: 32 4-bit samples in wave RAM, with NR32 output scaling.
    if ((active_channels & 0x04u) && (io[0x1A] & 0x80u)) {
        const auto frequency = static_cast<std::uint16_t>(io[0x1D] | ((io[0x1E] & 7u) << 8));
        const unsigned volume_code = (io[0x1C] >> 5) & 3u;
        if (frequency < 2048 && volume_code) {
            const double hz = 65536.0 / static_cast<double>(2048u - frequency);
            phase_[2] += hz / kSampleRate;
            phase_[2] -= std::floor(phase_[2]);
            const unsigned sample_index = static_cast<unsigned>(phase_[2] * 32.0) & 31u;
            const auto packed = io[0x30u + sample_index / 2u];
            const unsigned nibble = (sample_index & 1u) ? (packed & 0x0Fu) : (packed >> 4);
            const double scale = volume_code == 1 ? 1.0 : volume_code == 2 ? 0.5 : 0.25;
            channel[2] = ((static_cast<double>(nibble) - 7.5) / 7.5) * scale;
        }
    }

    // Channel 4: 15-bit LFSR noise, including the narrow 7-bit mode.
    if ((active_channels & 0x08u) && (io[0x21] & 0xF8u)) {
        const unsigned divisor_code = io[0x22] & 7u;
        const unsigned divisor = divisor_code == 0 ? 8u : divisor_code * 16u;
        const unsigned shift = (io[0x22] >> 4) & 0x0Fu;
        const double hz = 524288.0 / (static_cast<double>(divisor) * std::pow(2.0, shift + 1u));
        noise_phase_ += hz / kSampleRate;
        while (noise_phase_ >= 1.0) {
            noise_phase_ -= 1.0;
            const unsigned xor_bit = (noise_lfsr_ ^ (noise_lfsr_ >> 1)) & 1u;
            noise_lfsr_ = static_cast<std::uint16_t>((noise_lfsr_ >> 1) | (xor_bit << 14));
            if (io[0x22] & 0x08u)
                noise_lfsr_ = static_cast<std::uint16_t>((noise_lfsr_ & ~(1u << 6)) | (xor_bit << 6));
        }
        channel[3] = ((noise_lfsr_ & 1u) ? -1.0 : 1.0) *
                     static_cast<double>(envelope_volume_[2]) / 15.0;
    }

    // NR51 routes each channel independently to left and right. NR50 applies
    // the hardware's 1..8 output gain steps; samples are clipped to int16.
    const auto routing = io[0x25];
    const auto volume = io[0x24];
    double left = 0.0, right = 0.0;
    for (unsigned index = 0; index < 4; ++index) {
        if (routing & (1u << index)) right += channel[index];
        if (routing & (1u << (index + 4u))) left += channel[index];
    }
    left *= (((volume >> 4) & 7u) + 1u) / 8.0;
    right *= ((volume & 7u) + 1u) / 8.0;
    constexpr double gain = 5000.0;
    const auto l = static_cast<std::int16_t>(std::clamp(left * gain, -32768.0, 32767.0));
    const auto r = static_cast<std::int16_t>(std::clamp(right * gain, -32768.0, 32767.0));
    samples_.push_back(l);
    samples_.push_back(r);
}

std::vector<std::int16_t> AudioSynthesizer::take_samples() {
    std::vector<std::int16_t> result;
    result.swap(samples_);
    return result;
}

} // namespace oracles
