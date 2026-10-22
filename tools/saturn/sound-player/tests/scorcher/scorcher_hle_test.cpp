#include "scorcher_hle.hpp"

#include <saturn_sndlib/sequence.hpp>
#include <saturn_sndlib/tone_bank.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <ranges>
#include <string>
#include <vector>

int main() {
    std::string error;
    saturn_sfx::PcmTone tone;
    tone.samples.resize(4096);
    for (std::size_t i = 0; i < tone.samples.size(); ++i) {
        tone.samples[i] = static_cast<std::int16_t>(std::lround(
            std::sin(2.0 * std::numbers::pi * i / 64.0) * 12000.0));
    }
    const std::array tones{tone};
    const auto toneBank = saturn_sfx::PackPcmToneBank(tones, error);

    saturn_sfx::SequenceSong song;
    song.events = {
        {0, 0, 0xB0, 0x20, 0, 0},
        {0, 0, 0xC0, 0, 0, 0},
        {0, 240, 0x90, 69, 112, 0},
    };
    const std::array songs{song};
    const auto sequenceBank = saturn_sfx::PackSequenceBank(songs, error);
    if (!toneBank || !sequenceBank) {
        std::cerr << "cannot build Scorcher HLE fixture: " << error << '\n';
        return 1;
    }

    saturn_sfx::scorcher::MusImage image;
    image.areaMap = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x10, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0x05, 0x40,
        0x30, 0x04, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00,
        0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    image.payloads = {
        {0x00, 0x10000, 0, 0, *toneBank},
        {0x10, 0x20000, 0, 0, *sequenceBank},
        {0x20, 0x30000, 0, 0, std::vector<std::uint8_t>(0x540)},
    };

    saturn_sfx::scorcher::PlaybackResult result;
    if (!saturn_sfx::scorcher::HlePlayer::Render(
            image, 22050, result, error)) {
        std::cerr << "Scorcher HLE render failed: " << error << '\n';
        return 1;
    }
    const auto peak = std::ranges::max(
        result.samples | std::views::transform([](const auto& sample) {
            return std::max(std::abs(static_cast<int>(sample.left)),
                            std::abs(static_cast<int>(sample.right)));
        }));
    if (result.samples.size() != 22050 || peak == 0) {
        std::cerr << "Scorcher HLE rendered invalid PCM\n";
        return 1;
    }
    return 0;
}
