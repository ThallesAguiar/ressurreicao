#include <saturn_sndlib/hle_sequence_player.hpp>
#include <saturn_sndlib/package.hpp>
#include <saturn_sndlib/sequence.hpp>
#include <saturn_sndlib/sound_system.hpp>
#include <saturn_sndlib/tone_bank.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kToneOffset = 0x800;
constexpr std::size_t kSineFrames = 8192;
constexpr std::size_t kMaximumRenderFrames = 3 * 44100;
constexpr std::array<std::uint8_t, 7> kPhrase{
    67, 74, 79, 71, 74, 71, 67};

void WriteLE16(std::ostream& out, std::uint16_t value) {
    const std::array<char, 2> bytes{
        static_cast<char>(value), static_cast<char>(value >> 8)};
    out.write(bytes.data(), bytes.size());
}

void WriteLE32(std::ostream& out, std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value), static_cast<char>(value >> 8),
        static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    out.write(bytes.data(), bytes.size());
}

std::vector<std::int16_t> BuildSineSample() {
    std::vector<std::int16_t> samples(kSineFrames);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double phase = 2.0 * std::numbers::pi * 440.0 * i / 44100.0;
        samples[i] = static_cast<std::int16_t>(
            std::lround(std::sin(phase) * 12000.0));
    }
    return samples;
}

saturn_sfx::SequenceSong BuildSong() {
    saturn_sfx::SequenceSong song;
    song.events.push_back({0, 0, 0xB0, 0x20, 0, 0});
    song.events.push_back({0, 0, 0xC0, 0, 0, 0});
    for (std::size_t i = 0; i < kPhrase.size(); ++i) {
        song.events.push_back({
            i == 0 ? 0u : 120u, 96, 0x90, kPhrase[i], 112, 0});
    }
    return song;
}

bool WriteFile(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

bool WriteWav(
    const std::filesystem::path& path,
    std::span<const saturn_sfx::StereoSample> samples) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    const std::uint32_t dataSize = samples.size() * 4;
    out.write("RIFF", 4);
    WriteLE32(out, 36 + dataSize);
    out.write("WAVEfmt ", 8);
    WriteLE32(out, 16);
    WriteLE16(out, 1);
    WriteLE16(out, 2);
    WriteLE32(out, 44100);
    WriteLE32(out, 44100 * 4);
    WriteLE16(out, 4);
    WriteLE16(out, 16);
    out.write("data", 4);
    WriteLE32(out, dataSize);
    for (const auto sample : samples) {
        WriteLE16(out, static_cast<std::uint16_t>(sample.left));
        WriteLE16(out, static_cast<std::uint16_t>(sample.right));
    }
    return out.good();
}

} // namespace

int main(int argc, char** argv) {
    std::string error;
    const saturn_sfx::SequenceSong song = BuildSong();
    const std::array songs{song};
    const auto sequenceBank = saturn_sfx::PackSequenceBank(songs, error);
    saturn_sfx::PcmTone tone;
    tone.samples = BuildSineSample();
    const std::array tones{tone};
    const auto toneBank = saturn_sfx::PackPcmToneBank(tones, error);
    const std::array mapEntries{
        saturn_sfx::SoundAreaMapEntry{0x10, 0x10000, kToneOffset, true},
        saturn_sfx::SoundAreaMapEntry{0x00, 0x10800, 0x10000, true},
    };
    const auto areaMap = saturn_sfx::PackSoundAreaMap(mapEntries, error);
    if (!sequenceBank || !toneBank || !areaMap) {
        std::cerr << "cannot pack Mozart assets: " << error << '\n';
        return 1;
    }

    const saturn_sfx::SoundPackage package{
        0x10,
        0x10000,
        {
            {mapEntries[0], 0, *sequenceBank},
            {mapEntries[1], kToneOffset, *toneBank},
        },
    };
    const auto packed = saturn_sfx::PackSoundPackage(package, error);
    if (!packed) {
        std::cerr << "cannot pack Mozart CRT: " << error << '\n';
        return 1;
    }

    const auto packagePath =
        std::filesystem::temp_directory_path() / "saturn-sndlib-mozart.crt";
    if (!WriteFile(packagePath, *packed)) {
        std::cerr << "cannot write temporary Mozart CRT\n";
        return 1;
    }
    const auto loaded = ReadFile(packagePath);
    std::filesystem::remove(packagePath);
    const auto unpacked = saturn_sfx::UnpackSoundPackage(
        loaded, *areaMap, 0x10, error);
    const auto repacked = unpacked
                              ? saturn_sfx::PackSoundPackage(*unpacked, error)
                              : std::nullopt;
    if (!unpacked || !repacked || *repacked != loaded ||
        unpacked->areas.size() != 2) {
        std::cerr << "Mozart CRT did not round-trip: " << error << '\n';
        return 1;
    }

    auto decoder = saturn_sfx::SequenceDecoder::Open(
        unpacked->areas[0].data, 0, error);
    auto decodedTone = saturn_sfx::ToneBank::Open(
        unpacked->areas[1].data, mapEntries[1].address, error);
    if (!decoder || !decodedTone || decodedTone->VoiceCount() != 1) {
        std::cerr << "cannot unpack generated Mozart banks: " << error << '\n';
        return 1;
    }
    std::vector<saturn_sfx::SequenceEvent> decodedEvents;
    while (const auto event = decoder->Next(error))
        decodedEvents.push_back(*event);
    if (!error.empty() || decodedEvents != song.events) {
        std::cerr << "Mozart sequence did not round-trip semantically\n";
        return 1;
    }

    saturn_sfx::SoundSystem sound;
    if (!sound.BootHle(*areaMap, error) ||
        !sound.LoadArea(0x10, loaded, error) ||
        !sound.SetSequenceVolume(0, 128, 0, error)) {
        std::cerr << "smoke-test setup failed: " << error << '\n';
        return 1;
    }
    auto player = saturn_sfx::HleSequencePlayer::Create(
        unpacked->areas[0].data, 0, 0, 0x1E, error);
    if (!player) {
        std::cerr << "cannot start Mozart sequence: " << error << '\n';
        return 1;
    }
    for (std::size_t rendered = 0;
         rendered < kMaximumRenderFrames && !player->Finished();
         rendered += 128) {
        if (!player->Render(sound, 128, error)) {
            std::cerr << "Mozart render failed: " << error << '\n';
            return 1;
        }
    }
    sound.Render(2048);
    const auto peak = std::ranges::max(
        sound.Samples() | std::views::transform([](const auto& sample) {
            return std::max(std::abs(static_cast<int>(sample.left)),
                            std::abs(static_cast<int>(sample.right)));
        }));
    if (peak == 0) {
        std::cerr << "Mozart sequence rendered silence\n";
        return 1;
    }
    if (argc > 1 && !WriteWav(argv[1], sound.Samples())) {
        std::cerr << "cannot write " << argv[1] << '\n';
        return 1;
    }
    std::cout << "packed, reloaded, and rendered " << kPhrase.size()
              << " Mozart notes (peak=" << peak << ")\n";
    return 0;
}
