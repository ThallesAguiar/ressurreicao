#include <saturn_sndlib/hle_sequence_player.hpp>
#include <saturn_sndlib/sound_system.hpp>
#include "sotn_data.hpp"
#include "stage_sfx.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kChunkFrames = 128;
constexpr std::size_t kQuietFrames = 2048;
constexpr std::size_t kMaximumPlaybackFrames = 5 * 44100;
constexpr std::size_t kMaximumReleaseFrames = 5 * 44100;

struct Case {
    std::uint16_t id;
    saturn_sfx::sotn::SfxConfig config;
    std::string characterPackage;
    std::string stagePackage;
    std::string sourceOverlay;
};

struct RenderResult {
    std::vector<saturn_sfx::StereoSample> pcm;
    std::vector<saturn_sfx::SlotDebugState> firstVoices;
    std::vector<saturn_sfx::ScspTraceEvent> scspTrace;
};

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

bool IsPlayablePolicy(std::uint8_t policy) {
    return policy == 0 || policy == 1 || policy == 3 || policy == 5 ||
           policy == 9;
}

bool HasResidentSong(
    const std::filesystem::path& soundDirectory,
    const std::string& package,
    const saturn_sfx::sotn::SfxConfig& config) {
    if (config.bank < 2)
        return true;
    const auto data = Read(soundDirectory / package);
    const std::size_t offset = config.bank == 3 ? 0x800 : 0;
    if (offset + 2 > data.size())
        return false;
    const std::uint16_t count =
        static_cast<std::uint16_t>(data[offset]) << 8 | data[offset + 1];
    return config.song < count;
}

std::uint8_t SequenceSlot(std::uint8_t policy) {
    if (policy == 1)
        return 7;
    if (policy == 3)
        return 6;
    return 0;
}

std::vector<Case> BuildCases(
    std::span<const std::uint8_t> game,
    const std::filesystem::path& gameDirectory, std::string& error) {
    constexpr std::array<const char*, 3> characters{
        "SD_ALK.CRT", "SD_LIH.CRT", "SD_MARI.CRT"};
    std::map<std::string, Case> unique;
    for (const auto& stage :
         saturn_sfx::sotn::FindStageSoundSets(game, gameDirectory)) {
        const auto ids = saturn_sfx::sotn::ScanOverlaySfx(
            gameDirectory / stage.overlay, error);
        if (!error.empty())
            return {};
        for (const std::uint16_t id : ids) {
            if (id < saturn_sfx::sotn::kFirstSfxID ||
                id > saturn_sfx::sotn::kLastSfxID)
                continue;
            const auto found = saturn_sfx::sotn::GetSfxConfig(game, id);
            if (!found)
                continue;
            const auto config = *found;
            if (config.bank == 0) {
                unique.try_emplace(
                    "common:" + std::to_string(id),
                    Case{id, config, "SD_ALK.CRT", {}, stage.overlay});
            } else if (config.bank == 1) {
                for (const char* character : characters) {
                    unique.try_emplace(
                        std::string(character) + ':' + std::to_string(id),
                        Case{id, config, character, {}, stage.overlay});
                }
            } else if (config.bank == 2 || config.bank == 3) {
                if (!HasResidentSong(
                        gameDirectory / "SD", stage.soundPackage, config))
                    continue;
                unique.try_emplace(
                    stage.soundPackage + ':' + std::to_string(id),
                    Case{id, config, "SD_ALK.CRT", stage.soundPackage,
                         stage.overlay});
            } else {
                error = "SFX 0x";
                constexpr char hex[] = "0123456789ABCDEF";
                error += hex[id >> 8 & 0xF];
                error += hex[id >> 4 & 0xF];
                error += hex[id & 0xF];
                error += " uses unsupported sequence bank " +
                         std::to_string(config.bank);
                return {};
            }
        }
    }
    std::vector<Case> cases;
    cases.reserve(unique.size());
    for (auto& [key, testCase] : unique)
        cases.push_back(std::move(testCase));
    return cases;
}

bool ChunkIsSilent(
    std::span<const saturn_sfx::StereoSample> samples) {
    return std::ranges::all_of(samples, [](const auto& sample) {
        return sample.left == 0 && sample.right == 0;
    });
}

std::optional<RenderResult> Render(
    bool hle, std::span<const std::uint8_t> driver,
    std::span<const std::uint8_t> areaMap,
    const std::filesystem::path& soundDirectory,
    const std::vector<std::uint8_t>& common, const Case& testCase,
    std::string& error) {
    auto sound = std::make_unique<saturn_sfx::SoundSystem>();
    if ((hle ? !sound->BootHle(areaMap, error)
             : !sound->Boot(driver, areaMap, error)) ||
        !sound->LoadArea(0x10, common, error))
        return std::nullopt;

    std::vector<std::uint8_t> character;
    std::vector<std::uint8_t> stage;
    if (!testCase.characterPackage.empty()) {
        character = Read(soundDirectory / testCase.characterPackage);
        if (character.empty() || !sound->LoadArea(0x11, character, error))
            return std::nullopt;
    }
    if (!testCase.stagePackage.empty()) {
        stage = Read(soundDirectory / testCase.stagePackage);
        if (stage.empty() || !sound->LoadArea(0x12, stage, error))
            return std::nullopt;
    }

    RenderResult result;
    if (!IsPlayablePolicy(testCase.config.policy)) {
        result.pcm.resize(kQuietFrames);
        return result;
    }
    const std::uint8_t slot = SequenceSlot(testCase.config.policy);
    sound->BeginScspTrace();
    std::optional<saturn_sfx::HleSequencePlayer> player;
    if (hle) {
        std::span<const std::uint8_t> bank;
        if (testCase.config.bank == 0)
            bank = common;
        else if (testCase.config.bank == 1)
            bank = character;
        else
            bank = std::span(stage).subspan(
                testCase.config.bank == 3 ? 0x800 : 0);
        player = saturn_sfx::HleSequencePlayer::Create(
            bank, testCase.config.song, slot, testCase.config.priority, error);
        if (!player || !sound->SetSequenceVolume(
                           slot, testCase.config.volume, 0, error))
            return std::nullopt;
    } else if (!sound->StartSequence(
                   slot, testCase.config.bank, testCase.config.song,
                   testCase.config.priority, error) ||
               !sound->SetSequenceVolume(
                   slot, testCase.config.volume, 0, error)) {
        return std::nullopt;
    }

    sound->ClearSamples();
    bool heard = false;
    std::size_t quiet = 0;
    auto renderChunk = [&](std::size_t frames) {
        const std::size_t begin = sound->Samples().size();
        const bool okay = hle ? player->Render(*sound, frames, error)
                              : (sound->Render(frames), true);
        if (!okay)
            return false;
        const auto chunk = std::span(sound->Samples()).subspan(begin);
        if (ChunkIsSilent(chunk)) {
            if (heard)
                quiet += chunk.size();
        } else {
            if (result.firstVoices.empty()) {
                std::vector<saturn_sfx::SlotDebugState> candidate;
                for (const auto& voice : sound->DebugSlots()) {
                    if (voice.active)
                        candidate.push_back(voice);
                }
                if (!candidate.empty() &&
                    std::ranges::all_of(candidate, [](const auto& voice) {
                        return (voice.registers[0] & 0x0800) != 0;
                    }))
                    result.firstVoices = std::move(candidate);
            }
            heard = true;
            quiet = 0;
        }
        return true;
    };

    auto sequenceFinished = [&]() {
        return hle ? player->Finished()
                   : !sound->DebugSequence(slot).active;
    };
    for (std::size_t rendered = 0;
         rendered < kMaximumPlaybackFrames &&
         (!heard || !sequenceFinished() || quiet < kQuietFrames);
         rendered += kChunkFrames) {
        if (!renderChunk(kChunkFrames))
            return std::nullopt;
    }
    if (heard && quiet < kQuietFrames) {
        if (hle)
            player.reset();
        if (!sound->StopSequence(slot, error))
            return std::nullopt;
        for (std::size_t rendered = 0;
             rendered < kMaximumReleaseFrames &&
             quiet < kQuietFrames;
             rendered += kChunkFrames) {
            const std::size_t begin = sound->Samples().size();
            sound->Render(kChunkFrames);
            const auto chunk = std::span(sound->Samples()).subspan(begin);
            if (ChunkIsSilent(chunk)) {
                if (heard)
                    quiet += chunk.size();
            } else {
                heard = true;
                quiet = 0;
            }
        }
    }
    result.pcm = sound->Samples();
    result.scspTrace = sound->ScspTrace();
    return result;
}

struct PcmStats {
    bool audible = false;
    std::size_t first = 0;
    std::size_t last = 0;
    std::int32_t peak = 0;
    long double energy = 0;
};

PcmStats Stats(const std::vector<saturn_sfx::StereoSample>& pcm) {
    PcmStats stats;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const auto left = static_cast<std::int32_t>(pcm[i].left);
        const auto right = static_cast<std::int32_t>(pcm[i].right);
        if (left != 0 || right != 0) {
            if (!stats.audible)
                stats.first = i;
            stats.audible = true;
            stats.last = i;
        }
        stats.peak = std::max({stats.peak, std::abs(left), std::abs(right)});
        stats.energy += static_cast<long double>(left) * left +
                        static_cast<long double>(right) * right;
    }
    return stats;
}

bool Compare(
    const Case& testCase, const RenderResult& lle, const RenderResult& hle,
    std::string& reason) {
    // Compare SCSP programming, not sample-for-sample PCM. HLE issues a note
    // immediately while SOTN's v2.10-L MC68EC000 driver needs instructions to
    // consume the same command, so small timing differences are expected.
    // The LLE oracle follows PlaySfx (0x06011278) through func_060139C4 and the
    // Sega command-0x01 sequence-start path.
    const auto a = Stats(lle.pcm);
    const auto b = Stats(hle.pcm);
    if (a.audible != b.audible) {
        reason = "audibility differs";
        return false;
    }
    if (!a.audible)
        return true;
    const auto keyedTransitions = [](const RenderResult& result) {
        std::vector<const saturn_sfx::ScspTraceEvent*> transitions;
        for (const auto& event : result.scspTrace) {
            if (event.keyOn)
                transitions.push_back(&event);
        }
        return transitions;
    };
    const auto lleTransitions = keyedTransitions(lle);
    const auto hleTransitions = keyedTransitions(hle);
    if (lleTransitions.size() != hleTransitions.size()) {
        reason = "SCSP trace length differs: LLE=" +
                 std::to_string(lleTransitions.size()) + " HLE=" +
                 std::to_string(hleTransitions.size());
        return false;
    }
    if (!lleTransitions.empty()) {
        const std::uint64_t lleBase = lleTransitions.front()->sample;
        const std::uint64_t hleBase = hleTransitions.front()->sample;
        for (std::size_t i = 0; i < lleTransitions.size(); ++i) {
            const auto& x = *lleTransitions[i];
            const auto& y = *hleTransitions[i];
            if (x.keyOn != y.keyOn || x.registers != y.registers) {
                reason = "SCSP register transition " + std::to_string(i) +
                         " differs";
                return false;
            }
            const std::uint64_t xt = x.sample - lleBase;
            const std::uint64_t yt = y.sample - hleBase;
            if (std::max(xt, yt) - std::min(xt, yt) > 1024) {
                reason = "SCSP transition " + std::to_string(i) +
                         " timing differs: LLE=" + std::to_string(xt) +
                         " HLE=" + std::to_string(yt);
                return false;
            }
        }
    }
    const auto distance = [](std::size_t x, std::size_t y) {
        return std::max(x, y) - std::min(x, y);
    };
    if (distance(a.first, b.first) > 512) {
        reason = "PCM onset differs by over 512 frames";
        return false;
    }
    const std::size_t aDuration = a.last - a.first;
    const std::size_t bDuration = b.last - b.first;
    if (distance(aDuration, bDuration) > 2048) {
        reason = "audible duration differs: LLE=" +
                 std::to_string(aDuration) + " HLE=" +
                 std::to_string(bDuration);
        return false;
    }
    return true;
}

void DumpVoices(const char* backend, const RenderResult& result) {
    std::cerr << backend << " first active set:";
    for (const auto& voice : result.firstVoices) {
        std::cerr << " [slot=" << static_cast<unsigned>(voice.slot)
                  << " note=" << static_cast<unsigned>(voice.note)
                  << " layer=0x" << std::hex << voice.layerAddress
                  << " SA=0x" << voice.registers[0] << voice.registers[1]
                  << " OCT/FNS=0x" << voice.registers[8] << std::dec << ']';
    }
    std::cerr << '\n';
}

void DumpTrace(const char* backend, const RenderResult& result) {
    std::cerr << backend << " trace (" << result.scspTrace.size() << "):\n";
    for (std::size_t i = 0; i < result.scspTrace.size(); ++i) {
        const auto& event = result.scspTrace[i];
        std::cerr << "  " << i << " t=" << event.sample << ' '
                  << (event.keyOn ? "on " : "off") << " SA=0x" << std::hex
                  << event.registers[0] << event.registers[1] << " TL=0x"
                  << (event.registers[6] & 0xFF) << " pitch=0x"
                  << event.registers[8] << " env=0x" << event.registers[4]
                  << '/' << event.registers[5] << std::dec << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path root = SOTN_GAME_DIR;
    const auto game = Read(root / "0.BIN");
    const auto common = Read(root / "SD/SD_JO10.CRT");
    if (common.empty())
        return 1;
    std::string error;
    const auto boot = saturn_sfx::sotn::ExtractBootAssets(game, error);
    if (!boot) {
        std::cerr << "cannot extract SOTN sound assets: " << error << '\n';
        return 1;
    }
    const auto cases = BuildCases(game, root, error);
    if (!error.empty() || cases.empty()) {
        std::cerr << "cannot build overlay SFX corpus: " << error << '\n';
        return 1;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--list") {
        std::cout << cases.size() << " unique resident-package/SFX cases\n";
        return 0;
    }

    std::optional<std::uint16_t> selectedID;
    std::string selectedPackage;
    if (argc >= 3 && std::string_view(argv[1]) == "--case") {
        selectedID = static_cast<std::uint16_t>(
            std::stoul(argv[2], nullptr, 0));
        if (argc >= 4)
            selectedPackage = argv[3];
    }

    std::vector<const Case*> selectedCases;
    for (const auto& testCase : cases) {
        if (!selectedID ||
            (testCase.id == *selectedID &&
             (selectedPackage.empty() ||
              testCase.stagePackage == selectedPackage ||
              testCase.characterPackage == selectedPackage)))
            selectedCases.push_back(&testCase);
    }

    std::atomic_size_t nextCase = 0;
    std::atomic_size_t completed = 0;
    std::atomic_size_t failures = 0;
    std::mutex outputMutex;
    std::vector<std::string> failureMessages;
    const unsigned workerCount = selectedID
                                     ? 1
                                     : std::min(8u, std::max(
                                                        1u, std::thread::
                                                                hardware_concurrency()));
    auto worker = [&]() {
        while (failures.load() < 25) {
            const std::size_t index = nextCase.fetch_add(1);
            if (index >= selectedCases.size())
                return;
            const Case& testCase = *selectedCases[index];
            std::string caseError;
            const auto lle = Render(
                false, boot->driver, boot->areaMap,
                root / "SD", common,
                testCase, caseError);
            const auto hle = caseError.empty()
                                 ? Render(true, boot->driver, boot->areaMap,
                                          root / "SD", common,
                                          testCase, caseError)
                                 : std::nullopt;
            std::string reason;
            if (!lle || !hle || !Compare(testCase, *lle, *hle, reason)) {
                std::ostringstream out;
                out << "0x" << std::hex << testCase.id << std::dec << " ("
                    << testCase.sourceOverlay << ", "
                    << (testCase.stagePackage.empty()
                            ? testCase.characterPackage
                            : testCase.stagePackage)
                    << "): " << (caseError.empty() ? reason : caseError);
                {
                    std::lock_guard lock(outputMutex);
                    failureMessages.push_back(out.str());
                }
                ++failures;
            }
            if (selectedID && lle && hle) {
                std::lock_guard lock(outputMutex);
                DumpVoices("LLE", *lle);
                DumpVoices("HLE", *hle);
                DumpTrace("LLE", *lle);
                DumpTrace("HLE", *hle);
            }
            const std::size_t done = ++completed;
            if (!selectedID && done % 100 == 0) {
                std::lock_guard lock(outputMutex);
                std::cout << "verified " << done << '/'
                          << selectedCases.size() << " overlay SFX cases"
                          << std::endl;
            }
        }
    };
    std::vector<std::jthread> workers;
    workers.reserve(workerCount);
    for (unsigned i = 0; i < workerCount; ++i)
        workers.emplace_back(worker);
    workers.clear(); // joins all jthreads

    std::ranges::sort(failureMessages);
    for (const auto& message : failureMessages)
        std::cerr << message << '\n';
    if (failures != 0)
        std::cerr << failures << " overlay SFX comparisons failed\n";
    else
        std::cout << "verified all " << completed
                  << " overlay SFX cases against the 68000 driver\n";
    return failures == 0 ? 0 : 1;
}
