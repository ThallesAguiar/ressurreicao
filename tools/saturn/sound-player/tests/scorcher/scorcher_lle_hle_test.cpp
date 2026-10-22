#include "scorcher_hle.hpp"
#include "scorcher_lle.hpp"
#include "scorcher_mus.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

// Thirty seconds covers every shipped song's busy opening without paying the
// cost of full-song SCSP/DSP emulation. The 4096-frame tolerance is for
// accumulated MC68EC000 dispatch time: HLE applies events immediately while
// LLE executes the original driver between them. Registers and event order
// must still match. Run this test over LEVEL1..LEVEL6 and TITLE whenever
// sequence, tone, mixer, DSP, or driver-version behavior changes.
constexpr std::size_t kRenderFrames = 30 * 44100;
constexpr std::uint64_t kTimingTolerance = 4096;
std::mutex gOutputMutex;

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

struct PcmStats {
    std::size_t nonzero = 0;
    std::int32_t peak = 0;
};

PcmStats Stats(std::span<const saturn_sfx::StereoSample> samples) {
    PcmStats result;
    for (const auto sample : samples) {
        result.nonzero += sample.left != 0 || sample.right != 0;
        result.peak = std::max(
            {result.peak,
             std::abs(static_cast<std::int32_t>(sample.left)),
             std::abs(static_cast<std::int32_t>(sample.right))});
    }
    return result;
}

bool Check(const std::filesystem::path& path) {
    std::string error;
    const auto data = Read(path);
    const auto image = saturn_sfx::scorcher::ParseMus(data, error);
    if (!image) {
        std::lock_guard lock(gOutputMutex);
        std::cerr << path << ": " << error << '\n';
        return false;
    }
    saturn_sfx::scorcher::PlaybackResult lle;
    saturn_sfx::scorcher::PlaybackResult hle;
    if (!saturn_sfx::scorcher::LlePlayer::Render(
            *image, kRenderFrames, lle, error, true) ||
        !saturn_sfx::scorcher::HlePlayer::Render(
            *image, kRenderFrames, hle, error, true)) {
        std::lock_guard lock(gOutputMutex);
        std::cerr << path << ": render failed: " << error << '\n';
        return false;
    }
    const auto lleStats = Stats(lle.samples);
    const auto hleStats = Stats(hle.samples);
    if (lleStats.nonzero == 0 || hleStats.nonzero == 0) {
        std::lock_guard lock(gOutputMutex);
        std::cerr << path << ": one backend rendered silence\n";
        return false;
    }

    const auto keyEvents = [](const auto& trace) {
        std::vector<const saturn_sfx::ScspTraceEvent*> result;
        for (const auto& event : trace)
            if (event.keyOn)
                result.push_back(&event);
        return result;
    };
    const auto a = keyEvents(lle.scspTrace);
    const auto b = keyEvents(hle.scspTrace);
    if (!a.empty()) {
        const auto aBase = a.front()->sample;
        const auto bBase = b.front()->sample;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            auto aRegisters = a[i]->registers;
            auto bRegisters = b[i]->registers;
            // This byte is the global EFREG return mixer selected by physical
            // SCSP slot number, not part of the keyed voice. LLE and HLE can
            // allocate the same voice to different physical slots, so compare
            // the mixer independently rather than as note state.
            aRegisters[11] &= 0xFF00;
            bRegisters[11] &= 0xFF00;
            if (aRegisters != bRegisters) {
                std::lock_guard lock(gOutputMutex);
                std::cerr << path.filename()
                          << ": SCSP register event " << i
                          << " differs (LLE sample " << a[i]->sample
                          << ", HLE sample " << b[i]->sample
                          << ", OCT/FNS 0x" << std::hex
                          << a[i]->registers[8] << std::dec << ")\n";
                for (std::size_t reg = 0;
                     reg < a[i]->registers.size(); ++reg) {
                    if (aRegisters[reg] != bRegisters[reg])
                        std::cerr << "  reg " << reg << " LLE=0x"
                                  << std::hex << a[i]->registers[reg]
                                  << " HLE=0x" << b[i]->registers[reg]
                                  << std::dec << '\n';
                }
                return false;
            }
            const auto at = a[i]->sample - aBase;
            const auto bt = b[i]->sample - bBase;
            if (std::max(at, bt) - std::min(at, bt) > kTimingTolerance) {
                std::lock_guard lock(gOutputMutex);
                std::cerr << path.filename() << ": event " << i
                          << " timing differs: LLE=" << at
                          << " HLE=" << bt << '\n';
                return false;
            }
        }
        if (a.size() != b.size()) {
            const std::uint64_t commonHorizon = std::min(
                kRenderFrames -
                    std::min<std::uint64_t>(aBase, kRenderFrames),
                kRenderFrames -
                    std::min<std::uint64_t>(bBase, kRenderFrames));
            const auto& extra = a.size() > b.size() ? a : b;
            const std::size_t commonCount = std::min(a.size(), b.size());
            const bool tailOnly = std::ranges::all_of(
                extra.begin() + static_cast<std::ptrdiff_t>(commonCount),
                extra.end(), [&](const auto* event) {
                    return event->sample - extra.front()->sample +
                               kTimingTolerance >=
                           commonHorizon;
                });
            if (!tailOnly) {
                std::lock_guard lock(gOutputMutex);
                std::cerr << path.filename()
                          << ": key-event count differs away from render "
                             "boundary: LLE="
                          << a.size() << " HLE=" << b.size() << '\n';
                return false;
            }
        }
    }
    {
        std::lock_guard lock(gOutputMutex);
        std::cout << path.filename().string() << ": matched " << a.size()
                  << " key/register events in first 30s; LLE peak="
                  << lleStats.peak
                  << " HLE peak=" << hleStats.peak << std::endl;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: scorcher-lle-hle-test FILE.MUS [...]\n";
        return 2;
    }
    std::atomic_size_t nextPath = 1;
    std::atomic_size_t failures = 0;
    const unsigned workerCount = std::min<unsigned>(
        argc - 1, std::max(1u, std::thread::hardware_concurrency()));
    auto worker = [&]() {
        for (;;) {
            const std::size_t index = nextPath.fetch_add(1);
            if (index >= static_cast<std::size_t>(argc))
                return;
            if (!Check(argv[index]))
                ++failures;
        }
    };
    std::vector<std::jthread> workers;
    workers.reserve(workerCount);
    for (unsigned i = 0; i < workerCount; ++i)
        workers.emplace_back(worker);
    workers.clear();
    return failures == 0 ? 0 : 1;
}
