#ifdef _WIN32
#include "oracles/waveout_audio.hpp"

#include <algorithm>
#include <cstring>

namespace oracles {

WaveOutAudio::~WaveOutAudio() { shutdown(); }

bool WaveOutAudio::initialize() noexcept {
    shutdown();
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 32768;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    return waveOutOpen(&output_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
}

void WaveOutAudio::submit(const std::vector<std::int16_t>& samples) noexcept {
    if (!output_ || samples.empty()) return;
    std::size_t offset = 0;
    while (offset < samples.size()) {
        Slot* selected = nullptr;
        for (std::size_t attempt = 0; attempt < slots_.size(); ++attempt) {
            auto& slot = slots_[(next_slot_ + attempt) % slots_.size()];
            if (slot.prepared && !(slot.header.dwFlags & WHDR_DONE)) continue;
            if (slot.prepared) {
                waveOutUnprepareHeader(output_, &slot.header, sizeof(slot.header));
                slot.prepared = false;
            }
            selected = &slot;
            next_slot_ = (&slot - slots_.data() + 1) % slots_.size();
            break;
        }
        if (!selected) return; // Keep the emulation non-blocking if the device is behind.
        const auto count = std::min(selected->samples.size(), samples.size() - offset);
        std::copy_n(samples.data() + offset, count, selected->samples.data());
        selected->header = {};
        selected->header.lpData = reinterpret_cast<LPSTR>(selected->samples.data());
        selected->header.dwBufferLength = static_cast<DWORD>(count * sizeof(std::int16_t));
        if (waveOutPrepareHeader(output_, &selected->header, sizeof(selected->header)) != MMSYSERR_NOERROR)
            return;
        selected->prepared = true;
        if (waveOutWrite(output_, &selected->header, sizeof(selected->header)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(output_, &selected->header, sizeof(selected->header));
            selected->prepared = false;
            return;
        }
        offset += count;
    }
}

void WaveOutAudio::shutdown() noexcept {
    if (!output_) return;
    waveOutReset(output_);
    for (auto& slot : slots_) {
        if (slot.prepared) {
            waveOutUnprepareHeader(output_, &slot.header, sizeof(slot.header));
            slot.prepared = false;
        }
    }
    waveOutClose(output_);
    output_ = nullptr;
}

} // namespace oracles
#endif
