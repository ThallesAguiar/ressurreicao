#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace saturn_sfx::sotn {

constexpr std::uint16_t kFirstSfxID = 0x600;
constexpr std::size_t kSfxConfigCount = 791;
constexpr std::uint16_t kLastSfxID =
    kFirstSfxID + static_cast<std::uint16_t>(kSfxConfigCount) - 1;

struct BootAssets {
    std::span<const std::uint8_t> driver;
    std::span<const std::uint8_t> areaMap;
};

struct SfxConfig {
    std::uint8_t bank;
    std::uint8_t volume;
    std::uint8_t policy;
    std::uint8_t song;
    std::uint8_t priority;
};

struct SoundFile {
    std::string name;
    std::uint8_t area;
};

std::optional<BootAssets> ExtractBootAssets(
    std::span<const std::uint8_t> game, std::string& error);
std::optional<SfxConfig> GetSfxConfig(
    std::span<const std::uint8_t> game, std::uint16_t id);
std::vector<SoundFile> ReadSoundFiles(
    std::span<const std::uint8_t> game);
std::optional<std::uint8_t> FindSoundArea(
    std::span<const std::uint8_t> game, std::string_view filename);

} // namespace saturn_sfx::sotn
