#pragma once

#ifdef _WIN32
#include <array>
#include <cstdint>
#include <vector>
#include <mmsystem.h>

namespace oracles {

class WaveOutAudio {
public:
    WaveOutAudio() = default;
    ~WaveOutAudio();
    WaveOutAudio(const WaveOutAudio&) = delete;
    WaveOutAudio& operator=(const WaveOutAudio&) = delete;

    bool initialize() noexcept;
    void submit(const std::vector<std::int16_t>& interleaved_stereo) noexcept;
    void shutdown() noexcept;

private:
    struct Slot {
        WAVEHDR header{};
        std::array<std::int16_t, 4096> samples{};
        bool prepared{};
    };
    HWAVEOUT output_{};
    std::array<Slot, 4> slots_{};
    std::size_t next_slot_{};
};

} // namespace oracles
#endif
