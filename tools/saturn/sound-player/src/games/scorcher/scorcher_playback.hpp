#pragma once

#include <saturn_sndlib/sound_system.hpp>

#include <vector>

namespace saturn_sfx::scorcher {

struct PlaybackResult {
    std::vector<StereoSample> samples;
    std::vector<ScspTraceEvent> scspTrace;
};

} // namespace saturn_sfx::scorcher
