#include <saturn_sndlib/driver_version.hpp>
#include <saturn_sndlib/tone_bank.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

int main() {
    using saturn_sfx::ComputeScspPitch;
    using saturn_sfx::EncodeSequenceVolume;
    using saturn_sfx::PackPcmToneBank;
    using saturn_sfx::PcmTone;
    using saturn_sfx::SegaSoundDriverVersion;
    using saturn_sfx::ToneBank;

    if (EncodeSequenceVolume(SegaSoundDriverVersion::V1_31, 127) != 128 ||
        EncodeSequenceVolume(SegaSoundDriverVersion::V2_10L, 127) != 127) {
        std::cerr << "driver-specific sequence volume mismatch\n";
        return 1;
    }

    // The note-on pitch routine and FNS table are byte-identical in these
    // driver versions; keep one fixture for each versioned core policy.
    // Original routine/table offsets are cited beside ComputeScspPitch in
    // tone_bank.cpp.
    constexpr std::uint16_t expectedPitch = 0x003C;
    if (ComputeScspPitch(
            SegaSoundDriverVersion::V1_31, 69, 0, 70, 0) != expectedPitch ||
        ComputeScspPitch(
            SegaSoundDriverVersion::V2_10L, 69, 0, 70, 0) != expectedPitch) {
        std::cerr << "driver pitch fixture mismatch\n";
        return 1;
    }

    std::array<PcmTone, 1> tones;
    tones[0].samples = {0, 1000, -1000, 0};
    std::string error;
    const auto packed = PackPcmToneBank(tones, error);
    if (!packed) {
        std::cerr << "tone fixture pack failed: " << error << '\n';
        return 1;
    }
    const auto bank = ToneBank::Open(*packed, 0, error);
    if (!bank) {
        std::cerr << "tone fixture open failed: " << error << '\n';
        return 1;
    }
    const auto voice = bank->Voice(0, error);
    if (!voice || voice->layers.size() != 1) {
        std::cerr << "tone fixture voice failed: " << error << '\n';
        return 1;
    }

    std::uint16_t v131Velocity = 0;
    const auto v131Level = bank->TotalLevel(
        0, voice->layers[0], 59, 127,
        EncodeSequenceVolume(SegaSoundDriverVersion::V1_31, 127), error,
        &v131Velocity, SegaSoundDriverVersion::V1_31);
    std::uint16_t v210Velocity = 0;
    const auto v210Level = bank->TotalLevel(
        0, voice->layers[0], 59, 127,
        EncodeSequenceVolume(SegaSoundDriverVersion::V2_10L, 127), error,
        &v210Velocity, SegaSoundDriverVersion::V2_10L);
    if (!v131Level || *v131Level != 0x8F || v131Velocity != 14400) {
        std::cerr << "v1.31 TL fixture mismatch\n";
        return 1;
    }
    if (!v210Level || *v210Level != 0x93 || v210Velocity != 55) {
        std::cerr << "v2.10-L TL fixture mismatch\n";
        return 1;
    }
    return 0;
}
