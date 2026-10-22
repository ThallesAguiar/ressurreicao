#include "sotn_data.hpp"

#include <algorithm>
#include <cctype>

namespace saturn_sfx::sotn {

namespace {

// Retail Japanese SOTN 0.BIN offsets. The driver identifies itself as
// "Ver-2.10-L"; its system table is at driver+0x400. The two game tables are
// the file images of g_SaturnSoundFiles (runtime 0x0604128C) and
// g_SaturnSoundConfigs (runtime 0x06041655). Recheck these offsets against
// PlaySfx at 0x06011278 and the CRT loader beginning at 0x060129BC.
constexpr std::size_t kDriverOffset = 0x369FC;
constexpr std::size_t kDriverSize = 0x67F2;
constexpr std::size_t kAreaMapOffset = 0x369C0;
constexpr std::size_t kAreaMapSize = 58;
constexpr std::size_t kSoundFileTableOffset = 0x3D20C;
constexpr std::size_t kSoundFileCount = 57;
constexpr std::size_t kSoundFileRecordSize = 17;
constexpr std::size_t kSfxConfigOffset = 0x3D5D5;
constexpr std::size_t kSfxConfigSize = 5;

std::string Upper(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return result;
}

} // namespace

std::optional<BootAssets> ExtractBootAssets(
    std::span<const std::uint8_t> game, std::string& error) {
    const std::size_t required =
        std::max(kDriverOffset + kDriverSize, kAreaMapOffset + kAreaMapSize);
    if (game.size() < required) {
        error = "0.BIN is too small for the known Saturn revision";
        return std::nullopt;
    }
    const auto driver = game.subspan(kDriverOffset, kDriverSize);
    const auto areaMap = game.subspan(kAreaMapOffset, kAreaMapSize);
    if (driver[0x400] != 0x00 || driver[0x401] != 0x00 ||
        driver[0x402] != 0x04 || driver[0x403] != 0x40) {
        error = "sound driver does not have the expected v2.10-L system table";
        return std::nullopt;
    }
    return BootAssets{driver, areaMap};
}

std::optional<SfxConfig> GetSfxConfig(
    std::span<const std::uint8_t> game, std::uint16_t id) {
    // PlaySfx at 0x06011278 accepts 0x600..0x916, subtracts 0x600, and reads
    // one five-byte entry: bank, base volume, allocation policy, song, and
    // priority. Its request is eventually consumed by func_060139C4.
    if (id < kFirstSfxID || id > kLastSfxID)
        return std::nullopt;
    const std::size_t offset =
        kSfxConfigOffset + (id - kFirstSfxID) * kSfxConfigSize;
    if (offset + kSfxConfigSize > game.size())
        return std::nullopt;
    return SfxConfig{game[offset], game[offset + 1], game[offset + 2],
                     game[offset + 3], game[offset + 4]};
}

std::vector<SoundFile> ReadSoundFiles(
    std::span<const std::uint8_t> game) {
    // PlaySfx's 0xB00 command range indexes these 17-byte records. Byte 16 is
    // an area ID (normally 0x10..0x12), not a codec or SFX ID; the loader at
    // 0x060129BC resolves it through func_06012C4C and the live map at SH-2
    // address 0x25A00500.
    std::vector<SoundFile> result;
    result.reserve(kSoundFileCount);
    for (std::size_t i = 0; i < kSoundFileCount; ++i) {
        const std::size_t offset =
            kSoundFileTableOffset + i * kSoundFileRecordSize;
        if (offset + kSoundFileRecordSize > game.size())
            return {};
        const char* name = reinterpret_cast<const char*>(game.data() + offset);
        const auto end = std::find(name, name + 16, '\0');
        result.push_back({std::string(name, end), game[offset + 16]});
    }
    return result;
}

std::optional<std::uint8_t> FindSoundArea(
    std::span<const std::uint8_t> game, std::string_view filename) {
    const std::string wanted = Upper(filename);
    for (const auto& file : ReadSoundFiles(game)) {
        if (Upper(file.name) == wanted)
            return file.area;
    }
    return std::nullopt;
}

} // namespace saturn_sfx::sotn
