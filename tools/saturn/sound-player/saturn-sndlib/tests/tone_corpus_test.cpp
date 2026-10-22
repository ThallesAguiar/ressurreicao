#include <saturn_sndlib/tone_bank.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

namespace {

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

} // namespace

int main(int argc, char**) {
    const bool report = argc > 1;
    const std::filesystem::path directory =
        std::filesystem::path(SATURN_SNDLIB_SOTN_GAME_DIR) /
        "SD";
    constexpr std::array<std::size_t, 3> candidateOffsets{
        0x57BC, 0x0878, 0x1A60};
    std::size_t packages = 0;
    std::size_t voices = 0;
    std::size_t layers = 0;
    std::size_t fmLayers = 0;
    std::size_t pitchLfoLayers = 0;
    std::size_t pitchEnvelopeLayers = 0;
    std::size_t directLayers = 0;
    std::size_t effectLayers = 0;
    std::size_t monoVoices = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".CRT")
            continue;
        const auto data = Read(entry.path());
        bool found = false;
        for (const std::size_t offset : candidateOffsets) {
            if (offset >= data.size())
                continue;
            std::string error;
            auto tone = saturn_sfx::ToneBank::Open(
                std::span(data).subspan(offset), 0, error);
            if (!tone)
                continue;
            bool valid = true;
            for (std::size_t program = 0; program < tone->VoiceCount();
                 ++program) {
                error.clear();
                const auto voice = tone->Voice(
                    static_cast<std::uint8_t>(program), error);
                if (!voice) {
                    valid = false;
                    break;
                }
                ++voices;
                monoVoices +=
                    (voice->playModeAndBendRange & 0x70) != 0;
                for (const auto& layer : voice->layers) {
                    ++layers;
                    fmLayers += (layer.baseNote & 0x80) != 0;
                    pitchLfoLayers += (layer.sourceAddress & (1u << 30)) != 0;
                    pitchEnvelopeLayers +=
                        (layer.sourceAddress & (1u << 31)) != 0;
                    directLayers += (layer.directPan & 0xE0) != 0;
                    effectLayers += (layer.effectInput & 7) != 0;
                }
            }
            if (valid) {
                ++packages;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "could not locate tone bank in "
                      << entry.path().filename().string() << '\n';
            return 1;
        }
    }
    if (packages == 0)
        return 1;
    if (report) {
        std::cout << packages << " tone packages, " << voices << " voices, "
                  << layers << " layers; FM=" << fmLayers
                  << " PLFO=" << pitchLfoLayers
                  << " PEG=" << pitchEnvelopeLayers
                  << " direct=" << directLayers
                  << " effect=" << effectLayers
                  << " mono=" << monoVoices << '\n';
    }
    return 0;
}
