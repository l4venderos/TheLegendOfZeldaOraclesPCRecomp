#include "oracles/audio.hpp"

#include <algorithm>
#include <cmath>

namespace oracles {
namespace {
constexpr std::array<double, 4> kDutyThreshold{0.125, 0.25, 0.5, 0.75};
constexpr double kOutputGain = 4096.0;

double square_sample(double& phase, std::uint16_t frequency,
                     unsigned duty, unsigned volume) {
    if (frequency >= 2048 || volume == 0) return 0.0;
    const double hz = 131072.0 / static_cast<double>(2048u - frequency);
    phase += hz / AudioSynthesizer::kSampleRate;
    phase -= std::floor(phase);
    return (phase < kDutyThreshold[duty & 3u] ? 1.0 : -1.0) *
           static_cast<double>(volume) / 15.0;
}

std::int16_t clamp_sample(double sample) {
    sample = std::clamp(sample * kOutputGain, -32768.0, 32767.0);
    return static_cast<std::int16_t>(sample);
}
} // namespace

void AudioSynthesizer::reset() noexcept {
    sample_clock_ = 0;
    sample_count_ = 0;
    phase_.fill(0.0);
    noise_phase_ = 0.0;
    noise_lfsr_ = 0x7FFF;
    previous_channels_ = 0;
    envelope_volume_.fill(0);
    envelope_clock_.fill(0);
    samples_.clear();
    samples_.reserve(4096);
}

void AudioSynthesizer::tick_cpu_cycle(
    const std::array<std::uint8_t, 0x80>& io,
    bool powered, std::uint8_t active_channels, bool double_speed) {
    // Normal-speed CPU T-cycles run at 4.194304 MHz; CGB double-speed runs
    // at 8.388608 MHz while the APU remains clocked at the base rate.
    sample_clock_ += double_speed ? 1u : 2u;
    if (sample_clock_ >= 256u) {
        sample_clock_ -= 256u;
        synthesize(io, powered, active_channels);
    }
}

void AudioSynthesizer::synthesize(
    const std::array<std::uint8_t, 0x80>& io,
    bool powered, std::uint8_t active_channels) {
    ++sample_count_;
    if (!powered) {
        samples_.push_back(0);
        samples_.push_back(0);
        previous_channels_ = 0;
        return;
    }

    // Reset oscillator/envelope state when software triggers a channel.
    const auto rising = static_cast<std::uint8_t>(active_channels & ~previous_channels_);
    for (unsigned channel = 0; channel < 4; ++channel) {
        if (!(rising & (1u << channel))) continue;
        if (channel < 3) phase_[channel] = 0.0;
        else { noise_phase_ = 0.0; noise_lfsr_ = 0x7FFF; }
    }
    previous_channels_ = active_channels;

    // NR12, NR22 and NR42 envelopes are clocked at 64 Hz. Period zero means
    // the initial DAC level is held rather than periodically changing.
    if ((sample_count_ & 511u) == 0) {
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

    double channel[4]{};
    const auto f1 = static_cast<std::uint16_t>(io[0x13] | ((io[0x14] & 7u) << 8));
    const auto f2 = static_cast<std::uint16_t>(io[0x18] | ((io[0x19] & 7u) << 8));
    if ((active_channels & 0x01u) && (io[0x12] & 0xF8u)) {
        if (rising & 0x01u) envelope_volume_[0] = io[0x12] >> 4;
        channel[0] = square_sample(phase_[0], f1, io[0x11] >> 6, envelope_volume_[0]);
    }
    if ((active_channels & 0x02u) && (io[0x17] & 0xF8u)) {
        if (rising & 0x02u) envelope_volume_[1] = io[0x17] >> 4;
        channel[1] = square_sample(phase_[1], f2, io[0x16] >> 6, envelope_volume_[1]);
    }

    // Channel 3: 32 4-bit samples in wave RAM, selected by NR32 output level.
    if ((active_channels & 0x04u) && (io[0x1A] & 0x80u)) {
        const auto frequency = static_cast<std::uint16_t>(io[0x1D] | ((io[0x1E] & 7u) << 8));
        const unsigned volume_code = (io[0x1C] >> 5) & 3u;
        if (frequency < 2048 && volume_code) {
            const double hz = 65536.0 / static_cast<double>(2048u - frequency);
            phase_[2] += hz / kSampleRate;
            phase_[2] -= std::floor(phase_[2]);
            const unsigned index = static_cast<unsigned>(phase_[2] * 32.0) & 31u;
            const auto packed = io[0x30u + index / 2u];
            const unsigned nibble = (index & 1u) ? (packed & 0x0Fu) : (packed >> 4);
            const double scale = volume_code == 1 ? 1.0 : volume_code == 2 ? 0.5 : 0.25;
            channel[2] = ((static_cast<double>(nibble) - 7.5) / 7.5) * scale;
        }
    }

    // Channel 4: polynomial counter noise using the 15-bit LFSR and NR43
    // narrow (7-bit) mode. Its envelope is the NR42 envelope.
    if ((active_channels & 0x08u) && (io[0x21] & 0xF8u)) {
        if (rising & 0x08u) envelope_volume_[2] = io[0x21] >> 4;
        const unsigned divisor_code = io[0x22] & 7u;
        const double divisor = divisor_code == 0 ? 8.0 : divisor_code * 16.0;
        const unsigned shift = (io[0x22] >> 4) & 0x0Fu;
        const double hz = std::ldexp(524288.0 / divisor, -static_cast<int>(shift + 1u));
        noise_phase_ += hz / kSampleRate;
        while (noise_phase_ >= 1.0) {
            noise_phase_ -= 1.0;
            const unsigned feedback = (noise_lfsr_ ^ (noise_lfsr_ >> 1)) & 1u;
            noise_lfsr_ = static_cast<std::uint16_t>((noise_lfsr_ >> 1) | (feedback << 14));
            if (io[0x22] & 0x08u)
                noise_lfsr_ = static_cast<std::uint16_t>((noise_lfsr_ & ~(1u << 6)) | (feedback << 6));
        }
        channel[3] = ((noise_lfsr_ & 1u) ? -1.0 : 1.0) *
                     static_cast<double>(envelope_volume_[2]) / 15.0;
    }

    // NR51 routes each channel independently to left/right. NR50 controls
    // the two output gain fields. Output is clipped to signed 16-bit PCM.
    const auto routing = io[0x25];
    const auto volume = io[0x24];
    double left = 0.0, right = 0.0;
    for (unsigned index = 0; index < 4; ++index) {
        if (routing & (1u << index)) right += channel[index];
        if (routing & (1u << (index + 4u))) left += channel[index];
    }
    left *= static_cast<double>(((volume >> 4) & 7u) + 1u) / 8.0;
    right *= static_cast<double>((volume & 7u) + 1u) / 8.0;
    samples_.push_back(clamp_sample(left));
    samples_.push_back(clamp_sample(right));
}

std::vector<std::int16_t> AudioSynthesizer::take_samples() {
    std::vector<std::int16_t> result;
    result.swap(samples_);
    samples_.reserve(4096);
    return result;
}

} // namespace oracles
