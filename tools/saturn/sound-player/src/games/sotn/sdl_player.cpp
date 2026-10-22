#include "sdl_player.hpp"
#include <saturn_sndlib/hle_sequence_player.hpp>
#include "sotn_data.hpp"
#include "stage_sfx.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace saturn_sfx {

namespace {

constexpr int kAudioChunkFrames = 1024;
constexpr int kAudioTargetBytes = kAudioChunkFrames * 4 * 3;

struct SfxEntry {
    std::uint16_t id;
    sotn::SfxConfig config;
};

struct CharacterBank {
    const char* label;
    const char* file;
};

constexpr std::array<CharacterBank, 3> kCharacters{{
    {"ALUCARD", "SD_ALK.CRT"},
    {"RICHTER", "SD_LIH.CRT"},
    {"MARIA", "SD_MARI.CRT"},
}};

// SOTN keeps three replaceable packages resident together: common data at
// area 0x10, character data at 0x11, and the current stage at 0x12. This is
// visible in g_SaturnSoundFiles at 0x0604128C and the area lookup at
// 0x06012C4C. Selecting an ID with the wrong stage package is expected to
// address unrelated compiled sequence/tone data.

bool ReadFile(const std::filesystem::path& path,
              std::vector<std::uint8_t>& data, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path.string();
        return false;
    }
    data.assign(
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = "failed while reading " + path.string();
        return false;
    }
    return true;
}

std::vector<SfxEntry> BuildSfxList(const std::vector<std::uint8_t>& game) {
    std::vector<SfxEntry> entries;
    for (std::size_t i = 0; i < sotn::kSfxConfigCount; ++i) {
        const std::uint16_t id =
            sotn::kFirstSfxID + static_cast<std::uint16_t>(i);
        const auto config = sotn::GetSfxConfig(game, id);
        if (config)
            entries.push_back({id, *config});
    }
    return entries;
}

bool LoadPackage(
    SoundSystem& sound, const std::filesystem::path& soundDir,
    std::string_view filename, std::uint8_t area, std::string& error,
    std::vector<std::uint8_t>* loaded = nullptr,
    SoundSystem* mirror = nullptr) {
    std::vector<std::uint8_t> data;
    const auto path = soundDir / filename;
    if (!ReadFile(path, data, error) || !sound.LoadArea(area, data, error) ||
        (mirror && !mirror->LoadArea(area, data, error)))
        return false;
    if (loaded)
        *loaded = std::move(data);
    return true;
}

std::uint8_t SequenceSlot(
    const sotn::SfxConfig& config, std::uint8_t& pooledSlot) {
    if (config.policy == 1)
        return 7;
    if (config.policy == 3)
        return 6;
    const std::uint8_t slot = pooledSlot;
    pooledSlot = (pooledSlot + 1) % 6;
    return slot;
}

std::size_t WrapIndex(std::size_t current, std::size_t count, int delta) {
    const auto signedCount = static_cast<std::ptrdiff_t>(count);
    auto next = static_cast<std::ptrdiff_t>(current) + delta;
    next %= signedCount;
    if (next < 0)
        next += signedCount;
    return static_cast<std::size_t>(next);
}

void DrawRow(SDL_Renderer* renderer, float y, bool selected, const char* label,
             const std::string& value) {
    if (selected) {
        SDL_SetRenderDrawColor(renderer, 45, 57, 80, 255);
        const SDL_FRect rect{8.0f, y - 3.0f, 464.0f, 15.0f};
        SDL_RenderFillRect(renderer, &rect);
    }
    SDL_SetRenderDrawColor(renderer, selected ? 255 : 185, selected ? 225 : 195,
                           selected ? 120 : 210, 255);
    SDL_RenderDebugText(renderer, 14.0f, y, label);
    SDL_SetRenderDrawColor(renderer, 235, 235, 240, 255);
    SDL_RenderDebugText(renderer, 128.0f, y, value.c_str());
}

} // namespace

int sotn::RunSDLPlayer(
    SoundSystem& sound, const std::vector<std::uint8_t>& game,
    std::span<const std::uint8_t> areaMap,
    const std::filesystem::path& gameBin, bool startWithHle,
    const std::filesystem::path& screenshot) {
    const auto gameDirectory = gameBin.parent_path();
    const auto soundDir = gameDirectory / "SD";
    const std::vector<sotn::StageSoundSet> stages =
        sotn::FindStageSoundSets(game, gameDirectory);
    if (stages.empty()) {
        SDL_Log("No usable stage overlay/sound-package pairs found");
        return 1;
    }

    const std::vector<SfxEntry> sfxList = BuildSfxList(game);
    if (sfxList.size() != sotn::kSfxConfigCount) {
        SDL_Log("SFX configuration table is truncated");
        return 1;
    }

    std::string status = startWithHle ? "Ready (HLE driver)" : "Ready";
    std::string error;
    SoundSystem hleSound;
    if (!hleSound.BootHle(areaMap, error)) {
        SDL_Log("HLE sound initialization failed: %s", error.c_str());
        return 1;
    }
    std::vector<std::uint8_t> commonData;
    std::vector<std::uint8_t> characterData;
    std::vector<std::uint8_t> stageData;
    if (!LoadPackage(sound, soundDir, "SD_JO10.CRT", 0x10, error,
                     &commonData, &hleSound) ||
        !LoadPackage(sound, soundDir, kCharacters[0].file, 0x11, error,
                     &characterData, &hleSound) ||
        !LoadPackage(sound, soundDir, stages[0].soundPackage, 0x12, error,
                     &stageData, &hleSound)) {
        SDL_Log("Initial sound package load failed: %s", error.c_str());
        return 1;
    }
    std::vector<std::uint16_t> overlaySfx =
        sotn::ScanOverlaySfx(gameDirectory / stages[0].overlay, error);
    if (!error.empty()) {
        SDL_Log("Initial stage overlay scan failed: %s", error.c_str());
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            "SOTN Saturn SFX Player", 960, 540, SDL_WINDOW_RESIZABLE, &window,
            &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderLogicalPresentation(
        renderer, 480, 270, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    const SDL_AudioSpec spec{
        .format = SDL_AUDIO_S16,
        .channels = 2,
        .freq = 44100,
    };
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream || !SDL_ResumeAudioStreamDevice(stream)) {
        SDL_Log("Audio setup failed: %s", SDL_GetError());
        if (stream)
            SDL_DestroyAudioStream(stream);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int row = 0;
    std::size_t character = 0;
    std::size_t stage = 0;
    // 0x600 is a valid table entry but has volume zero. Start on the first
    // audible common effect so pressing Enter immediately demonstrates that
    // the selected backend is producing audio.
    std::size_t effect = 0x601 - sotn::kFirstSfxID;
    std::size_t overlayEffect = 0;
    bool overlayOnly = false;
    bool useHle = startWithHle;
    std::optional<HleSequencePlayer> hlePlayer;
    std::uint16_t hleEffectID = 0;
    std::uint64_t hleFramesRendered = 0;
    bool hleAudioReported = false;
    std::uint8_t pooledSlot = 0;
    bool running = true;
    int screenshotStatus = 0;
    const bool smokeTest = SDL_getenv("SATURN_SFX_SMOKE_TEST") != nullptr;
    int smokeFrames = 0;
    std::uint64_t smokeHleNonzero = 0;
    std::int32_t smokeHlePeak = 0;
    if (smokeTest) {
        SDL_Log("Initial sequence backend: %s", useHle ? "HLE" : "LLE");
        SDL_Log("%s: found %zu constant SFX IDs", stages[stage].overlay.c_str(),
                overlaySfx.size());
    }

    auto stopAndFlush = [&]() {
        std::string ignored;
        hleSound.StopAll(ignored);
        for (std::uint8_t slot = 0; slot < 8; ++slot)
            sound.StopSequence(slot, ignored);
        hlePlayer.reset();
        SDL_ClearAudioStream(stream);
    };

    auto reloadCharacter = [&]() {
        stopAndFlush();
        if (LoadPackage(
                sound, soundDir, kCharacters[character].file, 0x11, error,
                &characterData, &hleSound)) {
            status = std::string("Loaded character: ") +
                     kCharacters[character].label;
        } else {
            status = "ERROR: " + error;
        }
    };

    auto reloadStage = [&]() {
        stopAndFlush();
        if (!LoadPackage(
                sound, soundDir, stages[stage].soundPackage, 0x12, error,
                &stageData, &hleSound)) {
            status = "ERROR: " + error;
            return;
        }
        error.clear();
        overlaySfx =
            sotn::ScanOverlaySfx(
                gameDirectory / stages[stage].overlay, error);
        overlayEffect = 0;
        if (!error.empty()) {
            overlayOnly = false;
            status = "ERROR: " + error;
            return;
        }
        if (overlaySfx.empty())
            overlayOnly = false;
        if (smokeTest) {
            SDL_Log("%s: found %zu constant SFX IDs",
                    stages[stage].overlay.c_str(), overlaySfx.size());
        }
        status = "Loaded " + stages[stage].overlay + " / " +
                 stages[stage].soundPackage + " (" +
                 std::to_string(overlaySfx.size()) + " constant SFX IDs)";
    };

    auto currentEffect = [&]() -> const SfxEntry& {
        const std::uint16_t id =
            overlayOnly ? overlaySfx[overlayEffect] : sfxList[effect].id;
        return sfxList[id - sotn::kFirstSfxID];
    };

    auto sequenceBank = [&](std::uint8_t bank)
        -> std::span<const std::uint8_t> {
        const auto bankArea = sound.FindArea(0x10 | bank);
        if (!bankArea)
            return {};
        const std::array<std::pair<std::uint8_t,
                                  const std::vector<std::uint8_t>*>, 3>
            packages{{{0x10, &commonData},
                      {0x11, &characterData},
                      {0x12, &stageData}}};
        for (const auto& [areaID, data] : packages) {
            const auto area = sound.FindArea(areaID);
            if (area && bankArea->address >= area->address &&
                bankArea->address - area->address < data->size()) {
                return std::span(*data).subspan(
                    bankArea->address - area->address);
            }
        }
        return {};
    };

    auto playEffect = [&]() {
        const SfxEntry& entry = currentEffect();
        if (entry.config.policy == 2) {
            SDL_ClearAudioStream(stream);
            if (sound.StopSequence(7, error)) {
                char text[96];
                SDL_snprintf(text, sizeof(text),
                             "SFX 0x%03X stopped sequence slot 7", entry.id);
                status = text;
            } else {
                status = "ERROR: " + error;
            }
            return;
        }
        if (entry.config.policy == 4 || entry.config.policy == 6 ||
            entry.config.policy == 7 || entry.config.policy == 8) {
            char text[112];
            SDL_snprintf(
                text, sizeof(text), "SFX 0x%03X is a game no-op (policy %u)",
                entry.id, entry.config.policy);
            status = text;
            return;
        }
        if (entry.config.policy != 0 && entry.config.policy != 1 &&
            entry.config.policy != 3 && entry.config.policy != 5 &&
            entry.config.policy != 9) {
            status = "ERROR: unknown allocation policy";
            return;
        }
        const std::uint8_t slot = SequenceSlot(entry.config, pooledSlot);
        stopAndFlush();
        error.clear();
        if (useHle) {
            hlePlayer = HleSequencePlayer::Create(
                sequenceBank(entry.config.bank), entry.config.song, slot,
                entry.config.priority, error);
            if (hlePlayer && !hleSound.SetSequenceVolume(
                                 slot, entry.config.volume, 0, error))
                hlePlayer.reset();
            hleSound.ClearSamples();
            hleEffectID = entry.id;
            hleFramesRendered = 0;
            hleAudioReported = false;
        } else {
            hlePlayer.reset();
            if (!sound.StartSequence(slot, entry.config.bank,
                                     entry.config.song,
                                     entry.config.priority, error) ||
                !sound.SetSequenceVolume(slot, entry.config.volume, 0, error)) {
                // The common error handling below reports the driver failure.
            }
        }
        if (error.empty() && (!useHle || hlePlayer)) {
            char text[160];
            SDL_snprintf(text, sizeof(text),
                         "Playing 0x%03X (%s, bank %u, song %u, slot %u)",
                         entry.id, useHle ? "HLE" : "LLE", entry.config.bank,
                         entry.config.song, slot);
            status = text;
        } else {
            status = "ERROR: " + error;
        }
    };

    auto moveSelection = [&](int direction, bool fast) {
        if (row == 0) {
            useHle = !useHle;
            stopAndFlush();
            status = std::string("Sequence backend: ") +
                     (useHle ? "HLE" : "LLE");
        } else if (row == 1) {
            character = WrapIndex(character, kCharacters.size(), direction);
            reloadCharacter();
        } else if (row == 2) {
            stage = WrapIndex(stage, stages.size(), direction);
            reloadStage();
        } else if (row == 3) {
            if (overlaySfx.empty()) {
                overlayOnly = false;
                status = "No constant SFX IDs found in this overlay";
            } else {
                overlayOnly = !overlayOnly;
            }
        } else if (overlayOnly) {
            const int amount = fast ? 16 : 1;
            overlayEffect =
                WrapIndex(overlayEffect, overlaySfx.size(), direction * amount);
        } else {
            const int amount = fast ? 16 : 1;
            effect = WrapIndex(effect, sfxList.size(), direction * amount);
        }
    };

    while (running) {
        if (smokeTest) {
            switch (smokeFrames) {
            case 0:
                playEffect();
                break;
            case 2:
                character = WrapIndex(character, kCharacters.size(), 1);
                reloadCharacter();
                break;
            case 4:
                stage = WrapIndex(stage, stages.size(), 1);
                reloadStage();
                break;
            case 6:
                overlayOnly = !overlaySfx.empty();
                playEffect();
                break;
            case 8:
                useHle = true;
                overlayOnly = false;
                effect = 0x601 - sotn::kFirstSfxID;
                playEffect();
                break;
            default:
                break;
            }
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                switch (event.key.scancode) {
                case SDL_SCANCODE_ESCAPE:
                    running = false;
                    break;
                case SDL_SCANCODE_UP:
                    row = (row + 4) % 5;
                    break;
                case SDL_SCANCODE_DOWN:
                    row = (row + 1) % 5;
                    break;
                case SDL_SCANCODE_LEFT:
                    moveSelection(-1, false);
                    break;
                case SDL_SCANCODE_RIGHT:
                    moveSelection(1, false);
                    break;
                case SDL_SCANCODE_PAGEUP:
                    if (row == 4)
                        moveSelection(-1, true);
                    break;
                case SDL_SCANCODE_PAGEDOWN:
                    if (row == 4)
                        moveSelection(1, true);
                    break;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_SPACE:
                    playEffect();
                    break;
                case SDL_SCANCODE_S:
                    stopAndFlush();
                    status = "All sound stopped";
                    break;
                default:
                    break;
                }
            }
        }

        while (SDL_GetAudioStreamQueued(stream) < kAudioTargetBytes) {
            SoundSystem& activeSound = useHle ? hleSound : sound;
            activeSound.ClearSamples();
            if (hlePlayer) {
                if (!hlePlayer->Render(
                        activeSound, kAudioChunkFrames, error)) {
                    status = "ERROR: " + error;
                    hlePlayer.reset();
                    break;
                }
            } else {
                activeSound.Render(kAudioChunkFrames);
            }
            const auto& samples = activeSound.Samples();
            std::int32_t hleChunkPeak = 0;
            if (hlePlayer) {
                hleFramesRendered += samples.size();
                for (const StereoSample& sample : samples) {
                    hleChunkPeak = std::max(
                        hleChunkPeak,
                        std::abs(static_cast<std::int32_t>(sample.left)));
                    hleChunkPeak = std::max(
                        hleChunkPeak,
                        std::abs(static_cast<std::int32_t>(sample.right)));
                }
                if (!hleAudioReported && hleFramesRendered >= 44100 &&
                    hleChunkPeak == 0) {
                    char text[128];
                    SDL_snprintf(text, sizeof(text),
                                 "HLE 0x%03X generated one second of silence",
                                 hleEffectID);
                    status = text;
                    SDL_Log("%s", text);
                    hleAudioReported = true;
                }
            }
            if (smokeTest && hlePlayer) {
                smokeHlePeak = std::max(smokeHlePeak, hleChunkPeak);
                smokeHleNonzero += std::ranges::count_if(
                    samples, [](const StereoSample& sample) {
                        return sample.left != 0 || sample.right != 0;
                    });
            }
            if (!SDL_PutAudioStreamData(
                    stream, samples.data(),
                    static_cast<int>(samples.size() * sizeof(StereoSample)))) {
                status = std::string("Audio queue error: ") + SDL_GetError();
                break;
            }
            if (hlePlayer && hleChunkPeak >= 256 && !hleAudioReported) {
                char text[192];
                SDL_snprintf(
                    text, sizeof(text),
                    "HLE 0x%03X peak %d queued to SDL (%d bytes)",
                    hleEffectID, hleChunkPeak,
                    SDL_GetAudioStreamQueued(stream));
                status = text;
                SDL_Log("%s", text);
                hleAudioReported = true;
            }
        }

        const SfxEntry& current = currentEffect();
        char effectText[160];
        SDL_snprintf(
            effectText, sizeof(effectText),
            "0x%03X  bank=%u song=%u vol=%u pri=%u policy=%u", current.id,
            current.config.bank, current.config.song, current.config.volume,
            current.config.priority, current.config.policy);

        SDL_SetRenderDrawColor(renderer, 14, 18, 27, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 239, 198, 92, 255);
        SDL_RenderDebugText(renderer, 12.0f, 12.0f, "SOTN SATURN SFX PLAYER");
        SDL_SetRenderDrawColor(renderer, 135, 150, 175, 255);
        SDL_RenderDebugText(
            renderer, 12.0f, 27.0f,
            "Common SD_JO10.CRT is always resident. Arrows select.");

        DrawRow(renderer, 48.0f, row == 0, "BACKEND",
                useHle ? "HLE DRIVER" : "LLE 68000 DRIVER");
        DrawRow(renderer, 68.0f, row == 1, "CHARACTER",
                std::string(kCharacters[character].label) + "  " +
                    kCharacters[character].file);
        DrawRow(renderer, 88.0f, row == 2, "OVERLAY",
                stages[stage].overlay + "  " + stages[stage].soundPackage);
        DrawRow(renderer, 108.0f, row == 3, "SFX SET",
                overlayOnly ? "USED BY OVERLAY" : "ALL IDS");
        DrawRow(renderer, 128.0f, row == 4, "SFX ID", effectText);

        SDL_SetRenderDrawColor(renderer, 155, 170, 195, 255);
        SDL_RenderDebugText(
            renderer, 12.0f, 158.0f, "ENTER/SPACE play    S stop    ESC quit");
        SDL_RenderDebugText(
            renderer, 12.0f, 173.0f, "PAGE UP/DOWN skips 16 effects");
        SDL_SetRenderDrawColor(renderer, 100, 108, 126, 255);
        SDL_RenderLine(renderer, 10.0f, 196.0f, 470.0f, 196.0f);
        SDL_SetRenderDrawColor(renderer, 210, 215, 225, 255);
        SDL_RenderDebugText(renderer, 12.0f, 208.0f, status.c_str());
        SDL_SetRenderDrawColor(renderer, 112, 125, 145, 255);
        SDL_RenderDebugTextFormat(renderer, 12.0f, 238.0f,
                                  "%zu continuous IDs / %zu found in overlay",
                                  sfxList.size(), overlaySfx.size());
        if (!screenshot.empty()) {
            SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
            if (!surface) {
                SDL_Log("Screenshot capture failed: %s", SDL_GetError());
                screenshotStatus = 1;
            } else {
                const std::string path = screenshot.string();
                if (!SDL_SaveBMP(surface, path.c_str())) {
                    SDL_Log("Screenshot save failed: %s", SDL_GetError());
                    screenshotStatus = 1;
                } else {
                    SDL_Log("Saved screenshot to %s", path.c_str());
                }
                SDL_DestroySurface(surface);
            }
            running = false;
        }
        SDL_RenderPresent(renderer);
        if (smokeTest && ++smokeFrames >= 12)
            running = false;
        SDL_Delay(8);
    }

    SDL_DestroyAudioStream(stream);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!screenshot.empty())
        return screenshotStatus;
    if (smokeTest) {
        SDL_Log("HLE smoke-test peak: %d; nonzero frames: %llu",
                smokeHlePeak,
                static_cast<unsigned long long>(smokeHleNonzero));
        return smokeHlePeak < 256 ? 2 : 0;
    }
    return 0;
}

} // namespace saturn_sfx
