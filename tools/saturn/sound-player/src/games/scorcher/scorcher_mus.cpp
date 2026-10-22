#include "scorcher_mus.hpp"

#include <algorithm>
#include <string_view>

namespace saturn_sfx::scorcher {

namespace {

// These are offsets in the retail USA LEVEL*.MUS/TITLE.MUS container, not
// generic Sega driver constants. The embedded image is v1.31 and starts at
// file+0x24, so its version text is image+0x1020/file+0x1044. SDDRVS.TSK's
// loader at 0x00200ED0 established the header semantics below; its payload
// copy and area-resolution helpers are 0x00201B3C and 0x00202798.
constexpr std::size_t kPlayerVersionOffset = 0x1044;
constexpr std::string_view kPlayerVersion = "Ver1.31 95/06/20";
constexpr std::size_t kSoundRamSize = 512 * 1024;
constexpr std::size_t kDriverSize = 0x5D70;
constexpr std::size_t kAreaMapAddress = 0xA000;

std::uint32_t Read32(
    std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) << 24 |
           static_cast<std::uint32_t>(data[offset + 1]) << 16 |
           static_cast<std::uint32_t>(data[offset + 2]) << 8 |
           data[offset + 3];
}

struct AreaRecord {
    std::uint32_t address;
    std::uint32_t size;
    std::size_t offset;
};

std::optional<AreaRecord> FindArea(
    std::span<const std::uint8_t> map, std::uint8_t id) {
    for (std::size_t offset = 0; offset + 8 <= map.size(); offset += 8) {
        if (map[offset] != id) continue;
        const std::uint32_t address =
            static_cast<std::uint32_t>(map[offset + 1]) << 16 |
            static_cast<std::uint32_t>(map[offset + 2]) << 8 |
            map[offset + 3];
        return AreaRecord{address, Read32(map, offset + 4), offset};
    }
    return std::nullopt;
}

} // namespace

std::optional<MusImage> ParseMus(
    std::span<const std::uint8_t> data, std::string& error) {
    if (data.size() < kPlayerVersionOffset + kPlayerVersion.size()) {
        error = "Scorcher MUS file is truncated";
        return std::nullopt;
    }
    if (!std::ranges::equal(
            data.subspan(kPlayerVersionOffset, kPlayerVersion.size()),
            kPlayerVersion)) {
        error = "file does not contain the Scorcher Ver1.31 player";
        return std::nullopt;
    }

    MusImage result{};
    // Nine big-endian words: driver offset/4, map offset/4, map byte length,
    // then three (payload offset/4, byte length) pairs for areas 00, 10, 20.
    // Read the offsets rather than deriving contiguous positions: alignment
    // differs by file (LEVEL1's map is at 0x5D90 and payload begins 0x5DB6).
    for (std::size_t i = 0; i < result.header.size(); ++i)
        result.header[i] = Read32(data, i * 4);

    const std::size_t driverOffset = result.header[0] * 4u;
    const std::size_t mapOffset = result.header[1] * 4u;
    const std::size_t mapSize = result.header[2];
    if (result.header[0] != 9 || driverOffset + 8 > data.size() ||
        Read32(data, driverOffset) != 0xA000 ||
        Read32(data, driverOffset + 4) != 0x1000 || mapOffset < 4 ||
        mapOffset + mapSize > data.size() ||
        !std::ranges::equal(data.subspan(mapOffset - 4, 4),
                            std::string_view{"SEGA"})) {
        error = "Scorcher MUS header or reset vectors are not recognized";
        return std::nullopt;
    }

    if (driverOffset + kDriverSize > data.size() ||
        kAreaMapAddress + mapSize > kSoundRamSize) {
        error = "Scorcher MUS driver or area map is out of bounds";
        return std::nullopt;
    }

    result.soundRam.assign(kSoundRamSize, 0);
    std::ranges::copy(data.subspan(driverOffset, kDriverSize),
                      result.soundRam.begin());
    const auto map = data.subspan(mapOffset, mapSize);
    result.areaMap.assign(map.begin(), map.end());
    std::ranges::copy(map, result.soundRam.begin() + kAreaMapAddress);

    // SDDRVS.TSK passes these payload pairs to helper 0x00201B3C in this
    // order. Resolver 0x00202798 maps its indices to IDs 00 (tones), 10
    // (sequence), and 20 (DSP/effect data). It then sets bit 31 in the
    // corresponding current-map record at sound RAM 0x500; readyFlag* models
    // that handshake for the LLE frontend. Omitting the bit leaves the driver
    // consuming commands without ever keying on an SCSP slot.
    constexpr std::array<std::uint8_t, 3> kAreaIds{0x00, 0x10, 0x20};
    for (std::size_t i = 0; i < kAreaIds.size(); ++i) {
        const std::size_t source = result.header[3 + i * 2] * 4u;
        const std::size_t size = result.header[4 + i * 2];
        if (size == 0) continue;
        const auto area = FindArea(map, kAreaIds[i]);
        if (!area || source > data.size() || size > data.size() - source ||
            size > area->size || area->address >= kSoundRamSize ||
            size > kSoundRamSize - area->address) {
            error = "Scorcher MUS payload layout is out of sound RAM bounds";
            return std::nullopt;
        }
        result.payloads.push_back({
            kAreaIds[i],
            area->address,
            static_cast<std::uint32_t>(0x500 + area->offset + 4),
            area->size | 0x80000000u,
            std::vector<std::uint8_t>(data.begin() + source,
                                      data.begin() + source + size),
        });
    }
    return result;
}

} // namespace saturn_sfx::scorcher
