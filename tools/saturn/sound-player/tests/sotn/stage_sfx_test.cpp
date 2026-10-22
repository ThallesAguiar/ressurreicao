#include "stage_sfx.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kPlaySfx = 0x06011278;

void Write16(
    std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset + 0] = value >> 8;
    data[offset + 1] = value;
}

void Write32(
    std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    Write16(data, offset + 0, value >> 16);
    Write16(data, offset + 2, value);
}

bool ScanEquals(
    const std::vector<std::uint8_t>& data,
    std::span<const std::uint16_t> expected, const std::string& filename) {
    const auto path = std::filesystem::temp_directory_path() / filename;
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    std::string error;
    const auto actual = saturn_sfx::sotn::ScanOverlaySfx(path, error);
    std::filesystem::remove(path);
    return error.empty() && std::ranges::equal(actual, expected);
}

bool TestFarCall() {
    std::vector<std::uint8_t> data(0x80, 0x09); // pairs of SH-2 nops
    Write16(data, 0x00, 0x9406);                // mov.w @(0xC,pc),r4 -> 0x10
    Write16(data, 0x02, 0xD104);                // mov.l @(0x10,pc),r1 -> 0x14
    Write16(data, 0x04, 0xA02C);                // bra 0x60
    Write16(data, 0x06, 0x0009);                // delay-slot nop
    Write16(data, 0x10, 0x0793);
    Write32(data, 0x14, kPlaySfx);
    Write16(data, 0x60, 0x410B); // jsr @r1, well beyond 34 bytes
    Write16(data, 0x62, 0x0009);
    Write16(data, 0x64, 0x000B);
    Write16(data, 0x66, 0x0009);
    constexpr std::array<std::uint16_t, 1> expected{0x793};
    return ScanEquals(data, expected, "saturn-sfx-far-call.prg");
}

bool TestDelaySlotArgument() {
    std::vector<std::uint8_t> data(0x20, 0x09);
    Write16(data, 0x00, 0xD103); // mov.l @(0xC,pc),r1 -> 0x10
    Write16(data, 0x02, 0x410B); // jsr @r1
    Write16(data, 0x04, 0x9402); // delay: mov.w @(4,pc),r4 -> 0x0C
    Write16(data, 0x06, 0x000B);
    Write16(data, 0x08, 0x0009);
    Write16(data, 0x0C, 0x07CA);
    Write32(data, 0x10, kPlaySfx);
    constexpr std::array<std::uint16_t, 1> expected{0x7CA};
    return ScanEquals(data, expected, "saturn-sfx-delay-argument.prg");
}

} // namespace

int main() { return TestFarCall() && TestDelaySlotArgument() ? 0 : 1; }
