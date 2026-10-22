#pragma once

#include "scorcher_mus.hpp"
#include "scorcher_playback.hpp"

#include <cstddef>
#include <string>

namespace saturn_sfx::scorcher {

// Scorcher-specific HLE frontend. sndlib supplies generic Sega sequence and
// SCSP primitives; this class owns Scorcher's MUS conventions and policy.
class HlePlayer {
  public:
    static bool Render(const MusImage& image, std::size_t sampleCount,
                       PlaybackResult& result, std::string& error,
                       bool trace = false);
    static bool RenderThroughFirstLoop(
        const MusImage& image, std::size_t trailingSamples,
        std::size_t maximumSamples, PlaybackResult& result,
        std::string& error, bool trace = false);
};

} // namespace saturn_sfx::scorcher
