#include "wav.hpp"

#include <array>
#include <cstdint>
#include <fstream>

namespace saturn_sfx {

namespace {

void Put16(std::ostream& out, std::uint16_t value) {
    const std::array<char, 2> bytes{
        static_cast<char>(value), static_cast<char>(value >> 8)};
    out.write(bytes.data(), bytes.size());
}

void Put32(std::ostream& out, std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value), static_cast<char>(value >> 8),
        static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    out.write(bytes.data(), bytes.size());
}

} // namespace

bool WriteWav(const std::filesystem::path& path,
              std::span<const StereoSample> samples, std::string& error) {
    constexpr std::uint32_t kSampleRate = 44100;
    constexpr std::uint16_t kChannels = 2;
    constexpr std::uint16_t kBits = 16;
    constexpr std::uint16_t kBlockAlign = kChannels * (kBits / 8);
    constexpr std::uint32_t kByteRate = kSampleRate * kBlockAlign;

    const std::uint64_t dataSize64 = samples.size_bytes();
    if (dataSize64 > 0xFFFF'FFFFu - 36u) {
        error = "WAV output is too large";
        return false;
    }
    const auto dataSize = static_cast<std::uint32_t>(dataSize64);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "cannot open WAV output '" + path.string() + "'";
        return false;
    }

    out.write("RIFF", 4);
    Put32(out, 36 + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    Put32(out, 16);
    Put16(out, 1);
    Put16(out, kChannels);
    Put32(out, kSampleRate);
    Put32(out, kByteRate);
    Put16(out, kBlockAlign);
    Put16(out, kBits);
    out.write("data", 4);
    Put32(out, dataSize);
    for (const StereoSample sample : samples) {
        Put16(out, static_cast<std::uint16_t>(sample.left));
        Put16(out, static_cast<std::uint16_t>(sample.right));
    }
    if (!out) {
        error = "failed while writing WAV output '" + path.string() + "'";
        return false;
    }
    return true;
}

} // namespace saturn_sfx
