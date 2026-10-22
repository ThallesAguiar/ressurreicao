#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace saturn_sfx::sotn {

struct StageSoundSet {
    std::string overlay;
    std::string soundPackage;
};

std::vector<StageSoundSet> FindStageSoundSets(
    std::span<const std::uint8_t> game,
    const std::filesystem::path& gameDirectory);

std::vector<std::uint16_t> ScanOverlaySfx(
    const std::filesystem::path& overlay, std::string& error);

} // namespace saturn_sfx::sotn
