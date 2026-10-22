#include <saturn_sndlib/hle_sequence_player.hpp>
#include <saturn_sndlib/sound_system.hpp>
#include "scorcher_hle.hpp"
#include "scorcher_lle.hpp"
#include "scorcher_mus.hpp"
#include "sotn_data.hpp"
#include "wav.hpp"

#ifdef SATURN_SFX_HAVE_SDL
#include "sdl_player.hpp"
#endif

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::string command;
    std::filesystem::path gameBin = "0.BIN";
    std::filesystem::path crt;
    std::filesystem::path mus;
    std::filesystem::path output = "saturn-sfx.wav";
    std::filesystem::path screenshot;
    std::uint8_t area = 0x10;
    bool areaExplicit = false;
    std::uint8_t slot = 0;
    std::uint8_t bank = 0;
    std::uint8_t song = 0;
    std::uint8_t priority = 0x1E;
    std::uint8_t volume = 0x82;
    double seconds = 3.0;
    std::optional<std::uint16_t> sfx;
    bool hle = false;
};

void Usage(std::ostream& out) {
    out << "Usage:\n"
           "  saturn-sfx boot [--game-bin 0.BIN]\n"
           "  saturn-sfx play [--game-bin 0.BIN]\n"
           "  saturn-sfx render --crt FILE [options]\n"
           "  saturn-sfx scorcher --mus FILE [options]\n\n"
           "play options:\n"
           "  --screenshot FILE.bmp  save the initial UI frame and exit\n\n"
           "render options:\n"
           "  --area N       CRT area ID (default 0x10)\n"
           "  --sfx N        game SFX ID (0x600..0x916); selects "
           "bank/song/etc.\n"
           "  --slot N       sequence slot (default 0)\n"
           "  --bank N       sequence bank (default 0)\n"
           "  --song N       song/effect number (default 0)\n"
           "  --priority N   driver priority (default 0x1E)\n"
           "  --volume N     sequence volume (default 0x82)\n"
           "  --seconds N    output length (default 3)\n"
           "  --output FILE  WAV path (default saturn-sfx.wav)\n";
    out << "  --backend B    sequence backend: lle or hle (default lle)\n";
}

bool ParseByte(std::string_view text, std::uint8_t& value) {
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    unsigned parsed = 0;
    const auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), parsed, base);
    if (ec != std::errc{} || ptr != text.data() + text.size() || parsed > 255) {
        return false;
    }
    value = static_cast<std::uint8_t>(parsed);
    return true;
}

bool ParseU16(std::string_view text, std::uint16_t& value) {
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    unsigned parsed = 0;
    const auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), parsed, base);
    if (ec != std::errc{} || ptr != text.data() + text.size() ||
        parsed > 0xFFFF) {
        return false;
    }
    value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseDouble(std::string_view text, double& value) {
    std::string copy(text);
    char* end = nullptr;
    value = std::strtod(copy.c_str(), &end);
    return end == copy.c_str() + copy.size() && std::isfinite(value) &&
           value > 0.0 && value <= 600.0;
}

bool ParseOptions(int argc, char** argv, Options& options, std::string& error) {
    if (argc < 2) {
        error = "missing command";
        return false;
    }
    options.command = argv[1];
    if (options.command != "boot" && options.command != "render" &&
        options.command != "play" && options.command != "scorcher") {
        error = "unknown command '" + options.command + "'";
        return false;
    }

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (i + 1 >= argc) {
            error = "missing value after '" + std::string(arg) + "'";
            return false;
        }
        const std::string_view value = argv[++i];
        if (arg == "--game-bin") {
            options.gameBin = value;
        } else if (arg == "--crt") {
            options.crt = value;
        } else if (arg == "--mus") {
            options.mus = value;
        } else if (arg == "--output") {
            options.output = value;
        } else if (arg == "--screenshot") {
            options.screenshot = value;
        } else if (arg == "--seconds") {
            if (!ParseDouble(value, options.seconds)) {
                error = "invalid duration '" + std::string(value) + "'";
                return false;
            }
        } else if (arg == "--sfx") {
            std::uint16_t parsed;
            if (!ParseU16(value, parsed)) {
                error = "invalid SFX ID '" + std::string(value) + "'";
                return false;
            }
            options.sfx = parsed;
        } else if (arg == "--backend") {
            if (value != "lle" && value != "hle") {
                error = "--backend must be lle or hle";
                return false;
            }
            options.hle = value == "hle";
        } else {
            std::uint8_t* target = nullptr;
            if (arg == "--area") {
                target = &options.area;
                options.areaExplicit = true;
            }
            if (arg == "--slot")
                target = &options.slot;
            if (arg == "--bank")
                target = &options.bank;
            if (arg == "--song")
                target = &options.song;
            if (arg == "--priority")
                target = &options.priority;
            if (arg == "--volume")
                target = &options.volume;
            if (!target) {
                error = "unknown option '" + std::string(arg) + "'";
                return false;
            }
            if (!ParseByte(value, *target)) {
                error = "invalid byte value '" + std::string(value) + "'";
                return false;
            }
        }
    }

    if (options.command == "render" && options.crt.empty()) {
        error = "render requires --crt FILE";
        return false;
    }
    if (options.command == "scorcher" && options.mus.empty()) {
        error = "scorcher requires --mus FILE";
        return false;
    }
    if (!options.screenshot.empty() && options.command != "play") {
        error = "--screenshot is only valid with play";
        return false;
    }
    return true;
}

bool ReadFile(const std::filesystem::path& path,
              std::vector<std::uint8_t>& data, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    data.assign(
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = "failed while reading '" + path.string() + "'";
        return false;
    }
    return true;
}

bool ApplySfxConfig(const std::vector<std::uint8_t>& game, Options& options,
                    saturn_sfx::sotn::SfxConfig& config,
                    std::string& error) {
    if (!options.sfx)
        return true;
    const auto found = saturn_sfx::sotn::GetSfxConfig(game, *options.sfx);
    if (!found) {
        error = "SFX ID must be in the range 0x600..0x916";
        return false;
    }
    config = *found;
    options.bank = config.bank;
    options.volume = config.volume;
    options.song = config.song;
    options.priority = config.priority;

    switch (config.policy) {
    case 0:
    case 5:
    case 9:
        options.slot = 0; // first free pooled game slot for a fresh render
        break;
    case 1:
        options.slot = 7;
        break;
    case 3:
        options.slot = 6;
        break;
    case 2:
        error = "this SFX ID is a game command that stops sequence slot 7";
        return false;
    case 4:
    case 6:
    case 7:
    case 8:
        error = "this SFX ID is a no-op policy in the Saturn game service";
        return false;
    default:
        error = "unknown Saturn SFX allocation policy " +
                std::to_string(config.policy);
        return false;
    }
    return true;
}

bool LoadCrt(
    saturn_sfx::SoundSystem& sound, const std::vector<std::uint8_t>& game,
    const std::filesystem::path& path, std::optional<std::uint8_t> forcedArea,
    std::string& error, std::vector<std::uint8_t>* loaded = nullptr) {
    const auto area = forcedArea
                          ? forcedArea
                          : saturn_sfx::sotn::FindSoundArea(
                                game, path.filename().string());
    if (!area) {
        error = "cannot infer the sound area for CRT '" + path.string() +
                "'; specify --area";
        return false;
    }
    std::vector<std::uint8_t> data;
    if (!ReadFile(path, data, error) || !sound.LoadArea(*area, data, error)) {
        return false;
    }
    if (loaded)
        *loaded = data;
    std::cout << "loaded " << path.filename() << " at area 0x" << std::hex
              << static_cast<unsigned>(*area) << std::dec << " (" << data.size()
              << " bytes)\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error)) {
        std::cerr << "error: " << error << "\n\n";
        Usage(std::cerr);
        return 2;
    }

    if (options.command == "scorcher") {
        std::vector<std::uint8_t> data;
        if (!ReadFile(options.mus, data, error)) {
            std::cerr << "error: " << error << '\n';
            return 1;
        }
        const auto mus = saturn_sfx::scorcher::ParseMus(data, error);
        if (!mus) {
            std::cerr << "error: " << error << '\n';
            return 1;
        }
        const auto sampleCount = static_cast<std::size_t>(
            std::llround(options.seconds * 44100.0));
        saturn_sfx::scorcher::PlaybackResult result;
        const bool rendered = options.hle
                                  ? saturn_sfx::scorcher::HlePlayer::Render(
                                        *mus, sampleCount, result, error)
                                  : saturn_sfx::scorcher::LlePlayer::Render(
                                        *mus, sampleCount, result, error);
        if (!rendered || !saturn_sfx::WriteWav(
                             options.output, result.samples, error)) {
            std::cerr << "error: " << error << '\n';
            return 1;
        }
        std::int32_t peak = 0;
        std::uint64_t nonzero = 0;
        for (const auto sample : result.samples) {
            peak = std::max(
                peak, std::abs(static_cast<std::int32_t>(sample.left)));
            peak = std::max(
                peak, std::abs(static_cast<std::int32_t>(sample.right)));
            nonzero += sample.left != 0 || sample.right != 0;
        }
        std::cout << "rendered Scorcher MUS with separate "
                  << (options.hle ? "HLE" : "LLE") << " frontend\n"
                  << "wrote " << result.samples.size() << " samples to "
                  << options.output << " (peak=" << peak
                  << ", nonzero_frames=" << nonzero << ")\n";
        return peak == 0 ? 3 : 0;
    }

    std::vector<std::uint8_t> game;
    if (!ReadFile(options.gameBin, game, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }
    const auto boot = saturn_sfx::sotn::ExtractBootAssets(game, error);
    if (!boot) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    saturn_sfx::sotn::SfxConfig sfxConfig{};
    if (!ApplySfxConfig(game, options, sfxConfig, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    saturn_sfx::SoundSystem sound;
    const bool sdlPlayer = options.command == "play";
    if ((options.hle && !sdlPlayer
             ? !sound.BootHle(boot->areaMap, error)
             : !sound.Boot(boot->driver, boot->areaMap, error))) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    if (options.hle && !sdlPlayer) {
        std::cout << "driverless HLE sound system initialized\n";
    } else {
        std::cout << "driver booted: sys=0x" << std::hex
                  << sound.SystemInfoAddress() << " host=0x"
                  << sound.HostInterfaceAddress() << " map=0x"
                  << sound.CurrentAreaMapAddress() << std::dec << '\n';
    }
    if (options.command == "boot") {
        for (std::uint16_t id = 0; id <= 0xFF; ++id) {
            if (const auto area =
                    sound.FindArea(static_cast<std::uint8_t>(id))) {
                std::cout << "area 0x" << std::hex << id << ": address=0x"
                          << area->address << " size=0x" << area->size
                          << std::dec << '\n';
            }
        }
        return 0;
    }

    if (options.command == "play") {
#ifdef SATURN_SFX_HAVE_SDL
        return saturn_sfx::sotn::RunSDLPlayer(
            sound, game, boot->areaMap, options.gameBin, options.hle,
            options.screenshot);
#else
        std::cerr << "error: this build has no SDL3 support\n";
        return 1;
#endif
    }

    if (options.sfx) {
        std::cout << "SFX 0x" << std::hex << *options.sfx << std::dec
                  << ": bank=" << static_cast<unsigned>(sfxConfig.bank)
                  << " song=" << static_cast<unsigned>(sfxConfig.song)
                  << " volume=" << static_cast<unsigned>(sfxConfig.volume)
                  << " priority=" << static_cast<unsigned>(sfxConfig.priority)
                  << " policy=" << static_cast<unsigned>(sfxConfig.policy)
                  << " slot=" << static_cast<unsigned>(options.slot) << '\n';
    }

    // The game keeps SD_JO10 resident as the common package. Load it before a
    // character/stage package unless it is itself the requested CRT.
    const auto commonCrt = options.gameBin.parent_path() / "SD/SD_JO10.CRT";
    const bool requestedIsCommon =
        options.crt.filename() == commonCrt.filename();
    std::vector<std::uint8_t> commonData;
    std::vector<std::uint8_t> requestedData;
    if ((!requestedIsCommon &&
         !LoadCrt(sound, game, commonCrt, std::uint8_t{0x10}, error,
                  &commonData)) ||
        !LoadCrt(sound, game, options.crt,
                 options.areaExplicit
                     ? std::optional<std::uint8_t>{options.area}
                     : std::nullopt,
                 error, &requestedData)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    sound.ClearSamples();
    const auto sampleCount =
        static_cast<std::size_t>(std::llround(options.seconds * 44100.0));
    if (!options.hle) {
        if (!sound.StartSequence(options.slot, options.bank, options.song,
                                 options.priority, error) ||
            !sound.SetSequenceVolume(options.slot, options.volume, 0, error)) {
            std::cerr << "error: " << error << '\n';
            return 1;
        }
        sound.ClearSamples();
        sound.Render(sampleCount);
    } else {
        const auto bankArea = sound.FindArea(0x10 | options.bank);
        const auto requestedArea = sound.FindArea(
            options.areaExplicit ? options.area
                                 : *saturn_sfx::sotn::FindSoundArea(
                                       game,
                                       options.crt.filename().string()));
        const auto commonArea = sound.FindArea(0x10);
        if (!bankArea || !requestedArea || !commonArea) {
            error = "cannot resolve the HLE sequence bank";
        } else {
            std::span<const std::uint8_t> package;
            std::uint32_t packageAddress = 0;
            if (bankArea->address >= requestedArea->address &&
                bankArea->address - requestedArea->address <
                    requestedData.size()) {
                package = requestedData;
                packageAddress = requestedArea->address;
            } else if (!commonData.empty() &&
                       bankArea->address >= commonArea->address &&
                       bankArea->address - commonArea->address <
                           commonData.size()) {
                package = commonData;
                packageAddress = commonArea->address;
            }
            if (package.empty()) {
                error = "selected CRT does not contain sequence bank " +
                        std::to_string(options.bank);
            } else {
                package = package.subspan(bankArea->address - packageAddress);
                auto hle = saturn_sfx::HleSequencePlayer::Create(
                    package, options.song, options.slot, options.priority,
                    error);
                if (hle && sound.SetSequenceVolume(
                               options.slot, options.volume, 0, error)) {
                    sound.ClearSamples();
                }
                if (hle && error.empty() &&
                    hle->Render(sound, sampleCount, error)) {
                    std::cout << "rendered with HLE sequence decoder\n";
                }
            }
        }
        if (!error.empty()) {
            std::cerr << "error: " << error << '\n';
            return 1;
        }
    }
    if (!saturn_sfx::WriteWav(options.output, sound.Samples(), error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    std::int32_t peak = 0;
    std::uint64_t nonzero = 0;
    for (const auto sample : sound.Samples()) {
        peak = std::max(peak, std::abs(static_cast<std::int32_t>(sample.left)));
        peak =
            std::max(peak, std::abs(static_cast<std::int32_t>(sample.right)));
        nonzero += sample.left != 0 || sample.right != 0;
    }
    std::cout << "wrote " << sound.Samples().size() << " samples to "
              << options.output << " (peak=" << peak
              << ", nonzero_frames=" << nonzero << ")\n";
    return peak == 0 ? 3 : 0;
}
