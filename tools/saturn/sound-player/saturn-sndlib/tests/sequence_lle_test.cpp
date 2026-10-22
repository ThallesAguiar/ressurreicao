#include <saturn_sndlib/hle_sequence_player.hpp>
#include <saturn_sndlib/sequence.hpp>
#include <saturn_sndlib/sound_system.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

namespace {

constexpr std::size_t kDriverOffset = 0x369FC;
constexpr std::size_t kDriverSize = 0x67F2;
constexpr std::size_t kAreaMapOffset = 0x369C0;
constexpr std::size_t kAreaMapSize = 58;

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

bool Check(std::uint8_t song) {
    const std::filesystem::path root = SATURN_SNDLIB_SOTN_GAME_DIR;
    const auto game = Read(root / "0.BIN");
    const auto crt = Read(root / "SD/SD_JO10.CRT");
    if (game.size() < kDriverOffset + kDriverSize || crt.empty())
        return false;

    std::string error;
    auto decoder = saturn_sfx::SequenceDecoder::Open(crt, song, error);
    auto event = decoder ? decoder->Next(error) : std::nullopt;
    if (!event) {
        std::cerr << "HLE decode failed: " << error << '\n';
        return false;
    }

    saturn_sfx::SoundSystem lle;
    if (!lle.Boot(std::span(game).subspan(kDriverOffset, kDriverSize),
                  std::span(game).subspan(kAreaMapOffset, kAreaMapSize), error) ||
        !lle.LoadArea(0x10, crt, error) ||
        !lle.StartSequence(0, 0, song, 0x1E, error)) {
        std::cerr << "LLE setup failed: " << error << '\n';
        return false;
    }
    const auto state = lle.DebugSequence(0);
    const bool same = state.active && state.resolution == decoder->Resolution() &&
                      state.tempo ==
                          decoder->InitialTempo().microsecondsPerQuarter &&
                      state.status == event->status &&
                      state.data1 == event->data1 &&
                      state.data2 == event->data2 && state.gate == event->gate;
    if (!same) {
        std::cerr << "song " << static_cast<unsigned>(song)
                  << " differs from LLE: event=" << std::hex
                  << static_cast<unsigned>(event->status) << ' '
                  << static_cast<unsigned>(event->data1) << ' '
                  << static_cast<unsigned>(event->data2) << " gate="
                  << event->gate << "; LLE="
                  << static_cast<unsigned>(state.status) << ' '
                  << static_cast<unsigned>(state.data1) << ' '
                  << static_cast<unsigned>(state.data2) << " gate="
                  << state.gate << std::dec << '\n';
    }
    return same;
}

bool SameSample(const saturn_sfx::StereoSample& a,
                const saturn_sfx::StereoSample& b) {
    return a.left == b.left && a.right == b.right;
}

bool CheckAudio() {
    const std::filesystem::path root = SATURN_SNDLIB_SOTN_GAME_DIR;
    const auto game = Read(root / "0.BIN");
    const auto crt = Read(root / "SD/SD_JO10.CRT");
    const auto driver = std::span(game).subspan(kDriverOffset, kDriverSize);
    const auto map = std::span(game).subspan(kAreaMapOffset, kAreaMapSize);
    std::string error;

    saturn_sfx::SoundSystem lle;
    if (!lle.Boot(driver, map, error) || !lle.LoadArea(0x10, crt, error) ||
        !lle.StartSequence(0, 0, 0, 0x1E, error) ||
        !lle.SetSequenceVolume(0, 0x82, 0, error))
        return false;
    lle.ClearSamples();
    lle.Render(10000);

    saturn_sfx::SoundSystem hleSound;
    auto hle = saturn_sfx::HleSequencePlayer::Create(
        crt, 0, 0, 0x1E, error);
    if (!hle || !hleSound.Boot(driver, map, error) ||
        !hleSound.LoadArea(0x10, crt, error) ||
        !hleSound.SetSequenceVolume(0, 0x82, 0, error))
        return false;
    hleSound.ClearSamples();
    if (!hle->Render(hleSound, 5000, error) ||
        !hle->Render(hleSound, 5000, error))
        return false;

    const auto& a = lle.Samples();
    const auto& b = hleSound.Samples();
    for (std::size_t shift = 0; shift <= 128; ++shift) {
        if (a.size() <= shift || b.size() <= shift)
            break;
        bool equal = true;
        for (std::size_t i = 0; i + shift < a.size() && i < b.size(); ++i) {
            if (!SameSample(a[i + shift], b[i])) {
                equal = false;
                break;
            }
        }
        if (equal)
            return true;
        equal = true;
        for (std::size_t i = 0; i + shift < b.size() && i < a.size(); ++i) {
            if (!SameSample(a[i], b[i + shift])) {
                equal = false;
                break;
            }
        }
        if (equal)
            return true;
    }
    std::cerr << "HLE PCM does not equal LLE PCM within 128 samples\n";
    return false;
}

} // namespace

int main() {
    return Check(0) && Check(1) && Check(127) && CheckAudio() ? 0 : 1;
}
