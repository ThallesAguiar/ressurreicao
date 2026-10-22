#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace saturn_sfx {

struct SoundAreaMapEntry {
    std::uint8_t id;
    std::uint32_t address;
    std::uint32_t size;
    bool loadable;
};

struct SoundPackageArea {
    SoundAreaMapEntry map;
    std::size_t offset;
    std::vector<std::uint8_t> data;
};

struct SoundPackage {
    std::uint8_t baseArea;
    std::uint32_t baseAddress;
    std::vector<SoundPackageArea> areas;
};

std::optional<std::vector<SoundAreaMapEntry>> ParseSoundAreaMap(
    std::span<const std::uint8_t> data, std::string& error);
std::optional<std::vector<std::uint8_t>> PackSoundAreaMap(
    std::span<const SoundAreaMapEntry> entries, std::string& error);
std::optional<SoundPackage> UnpackSoundPackage(
    std::span<const std::uint8_t> data,
    std::span<const std::uint8_t> areaMap, std::uint8_t baseArea,
    std::string& error);
std::optional<std::vector<std::uint8_t>> PackSoundPackage(
    const SoundPackage& package, std::string& error);

} // namespace saturn_sfx
