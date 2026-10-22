#pragma once

#include <algorithm>
#include <cstdint>

namespace saturn_sfx {

// These select behavior observed in two retail Sega driver images:
//   V1_31: Scorcher LEVEL1.MUS+0x24, 0x5D70 bytes
//   V2_10L: SOTN 0.BIN+0x369FC, 0x67F2 bytes
// They share Sega formats and about 82% of Scorcher's image, but are not
// address- or behavior-compatible. Keep revision differences behind this
// selector and verify each one against both original-driver LLE paths.
enum class SegaSoundDriverVersion : std::uint16_t {
    V1_31 = 0x0131,
    V2_10L = 0x0210,
};

constexpr std::uint8_t EncodeSequenceVolume(
    SegaSoundDriverVersion version, std::uint8_t volume) {
    const unsigned bias =
        version == SegaSoundDriverVersion::V1_31 ? 1u : 0u;
    return static_cast<std::uint8_t>(
        std::min<unsigned>(static_cast<unsigned>(volume) + bias, 255u));
}

} // namespace saturn_sfx
