#pragma once

#include <saturn_sndlib/sound_system.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace saturn_sfx::sotn {

int RunSDLPlayer(SoundSystem& sound, const std::vector<std::uint8_t>& game,
                 std::span<const std::uint8_t> areaMap,
                 const std::filesystem::path& gameBin, bool startWithHle,
                 const std::filesystem::path& screenshot = {});

} // namespace saturn_sfx::sotn
