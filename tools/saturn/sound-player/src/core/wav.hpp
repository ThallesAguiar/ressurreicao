#pragma once

#include <saturn_sndlib/sound_system.hpp>

#include <filesystem>
#include <span>
#include <string>

namespace saturn_sfx {

bool WriteWav(const std::filesystem::path& path,
              std::span<const StereoSample> samples, std::string& error);

} // namespace saturn_sfx
