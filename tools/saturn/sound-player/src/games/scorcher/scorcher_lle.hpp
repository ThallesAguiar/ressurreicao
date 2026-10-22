#pragma once

#include "scorcher_mus.hpp"
#include "scorcher_playback.hpp"

#include <cstddef>
#include <string>

namespace saturn_sfx::scorcher {

class LlePlayer {
  public:
    static bool Render(const MusImage& image, std::size_t sampleCount,
                       PlaybackResult& result, std::string& error,
                       bool trace = false);
};

} // namespace saturn_sfx::scorcher
