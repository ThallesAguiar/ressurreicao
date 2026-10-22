#include <saturn_sndlib/sound_system.hpp>
#include <saturn_sndlib/tone_bank.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

namespace {

constexpr std::size_t kDriverOffset = 0x369FC;
constexpr std::size_t kDriverSize = 0x67F2;
constexpr std::size_t kAreaMapOffset = 0x369C0;
constexpr std::size_t kAreaMapSize = 58;

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

bool CompareCase(const std::vector<std::uint8_t>& game,
                 const std::vector<std::uint8_t>& crt,
                 const saturn_sfx::ToneBank& tone, std::uint8_t program,
                 std::uint8_t note) {
    std::string error;
    const auto selected = tone.SelectLayers(program, note, error);
    if (selected.empty())
        return error.empty();

    saturn_sfx::SoundSystem lle;
    if (!lle.Boot(std::span(game).subspan(kDriverOffset, kDriverSize),
                  std::span(game).subspan(kAreaMapOffset, kAreaMapSize), error) ||
        !lle.LoadArea(0x10, crt, error) ||
        !lle.SendMidi(0, 0x1E, 0xB0, 0x20, 0, error) ||
        !lle.SendMidi(0, 0x1E, 0xC0, program, 0, error) ||
        !lle.SendMidi(0, 0x1E, 0x90, note, 127, error)) {
        std::cerr << "LLE setup failed: " << error << '\n';
        return false;
    }
    lle.Render(256);

    saturn_sfx::SoundSystem hle;
    if (!hle.BootHle(
            std::span(game).subspan(kAreaMapOffset, kAreaMapSize), error) ||
        !hle.LoadArea(0x10, crt, error) ||
        !hle.SendMidi(0, 0x1E, 0xB0, 0x20, 0, error) ||
        !hle.SendMidi(0, 0x1E, 0xC0, program, 0, error) ||
        !hle.SendMidi(0, 0x1E, 0x90, note, 127, error)) {
        std::cerr << "HLE setup failed: " << error << '\n';
        return false;
    }
    hle.Render(256);

    std::vector<std::uint32_t> expected;
    for (const auto& layer : selected)
        expected.push_back(tone.SoundAddress() + layer.offset);
    std::ranges::sort(expected);

    const auto lleSlots = lle.DebugSlots();
    std::vector<std::uint32_t> actual;
    for (const auto& slot : lleSlots) {
        if (slot.active && slot.midiChannel == 0 && slot.note == note) {
            actual.push_back(slot.layerAddress);
            const auto layer = std::ranges::find_if(
                selected, [&](const auto& candidate) {
                    return tone.SoundAddress() + candidate.offset ==
                           slot.layerAddress;
                });
            if (layer == selected.end())
                continue;
            const auto image = saturn_sfx::BuildScspSlotImage(
                *layer, tone.SoundAddress(), note);
            for (std::size_t reg = 0; reg < image.registers.size(); ++reg) {
                const std::uint16_t difference = static_cast<std::uint16_t>(
                    (slot.registers[reg] ^ image.registers[reg]) &
                    image.toneMask[reg]);
                if (difference != 0) {
                    std::cerr << "program " << static_cast<unsigned>(program)
                              << " note " << static_cast<unsigned>(note)
                              << " register " << reg << " is 0x" << std::hex
                              << slot.registers[reg] << ", expected 0x"
                              << image.registers[reg] << " under mask 0x"
                              << image.toneMask[reg] << std::dec << '\n';
                    return false;
                }
            }
            const auto totalLevel =
                tone.TotalLevel(program, *layer, 127, 127, 128, error);
            if (!totalLevel ||
                (slot.registers[6] & 0xFF) != *totalLevel) {
                std::cerr << "program " << static_cast<unsigned>(program)
                          << " note " << static_cast<unsigned>(note)
                          << " TL is 0x" << std::hex
                          << (slot.registers[6] & 0xFF) << ", expected 0x"
                          << (totalLevel ? static_cast<unsigned>(*totalLevel)
                                         : 0)
                          << std::dec << '\n';
                return false;
            }
        }
    }
    std::ranges::sort(actual);
    if (actual != expected) {
        std::cerr << "program " << static_cast<unsigned>(program) << " note "
                  << static_cast<unsigned>(note) << " selected "
                  << actual.size() << " LLE layers, expected "
                  << expected.size() << '\n';
        return false;
    }

    std::vector<std::uint32_t> hleLayers;
    for (const auto& slot : hle.DebugSlots()) {
        if (!slot.active || slot.midiChannel != 0 || slot.note != note)
            continue;
        hleLayers.push_back(slot.layerAddress);
        const auto lleSlot = std::ranges::find_if(
            lleSlots, [&](const auto& candidate) {
                return candidate.active &&
                       candidate.layerAddress == slot.layerAddress;
            });
        if (lleSlot == lleSlots.end()) {
            std::cerr << "HLE allocated a layer absent from the LLE result\n";
            return false;
        }
        const auto layer = std::ranges::find_if(
            selected, [&](const auto& candidate) {
                return tone.SoundAddress() + candidate.offset ==
                       slot.layerAddress;
            });
        const auto image = saturn_sfx::BuildScspSlotImage(
            *layer, tone.SoundAddress(), note);
        for (std::size_t reg = 0; reg < image.registers.size(); ++reg) {
            if (((slot.registers[reg] ^ lleSlot->registers[reg]) &
                 image.toneMask[reg]) != 0) {
                std::cerr << "HLE/LLE register mismatch for program "
                          << static_cast<unsigned>(program) << " note "
                          << static_cast<unsigned>(note) << " register "
                          << reg << '\n';
                return false;
            }
        }
        if ((slot.registers[6] & 0xFF) !=
            (lleSlot->registers[6] & 0xFF)) {
            std::cerr << "HLE/LLE total-level mismatch: HLE=0x" << std::hex
                      << (slot.registers[6] & 0xFF) << " LLE=0x"
                      << (lleSlot->registers[6] & 0xFF) << std::dec << '\n';
            return false;
        }
    }
    std::ranges::sort(hleLayers);
    if (hleLayers != actual) {
        std::cerr << "HLE/LLE allocated layer sets differ\n";
        return false;
    }
    return true;
}

bool CompareControllers(const std::vector<std::uint8_t>& game,
                        const std::vector<std::uint8_t>& crt) {
    std::string error;
    saturn_sfx::SoundSystem lle;
    saturn_sfx::SoundSystem hle;
    const auto driver = std::span(game).subspan(kDriverOffset, kDriverSize);
    const auto map = std::span(game).subspan(kAreaMapOffset, kAreaMapSize);
    if (!lle.Boot(driver, map, error) || !hle.BootHle(map, error) ||
        !lle.LoadArea(0x10, crt, error) || !hle.LoadArea(0x10, crt, error)) {
        std::cerr << "controller test setup failed: " << error << '\n';
        return false;
    }
    auto send = [&](saturn_sfx::SoundSystem& sound) {
        return sound.SendMidi(0, 0x1E, 0xB0, 0x20, 0, error) &&
               sound.SendMidi(0, 0x1E, 0xC0, 0, 0, error) &&
               sound.SendMidi(0, 0x1E, 0xB0, 0x07, 91, error) &&
               sound.SendMidi(0, 0x1E, 0xB0, 0x0A, 12, error) &&
               sound.SendMidi(0, 0x1E, 0x90, 24, 37, error);
    };
    if (!send(lle) || !send(hle)) {
        std::cerr << "controller test MIDI failed: " << error << '\n';
        return false;
    }
    lle.Render(256);
    hle.Render(256);
    const auto find = [](const auto& slots) {
        return std::ranges::find_if(slots, [](const auto& slot) {
            return slot.active && slot.midiChannel == 0 && slot.note == 24;
        });
    };
    const auto lleSlots = lle.DebugSlots();
    const auto hleSlots = hle.DebugSlots();
    const auto lleSlot = find(lleSlots);
    const auto hleSlot = find(hleSlots);
    if (lleSlot == lleSlots.end() || hleSlot == hleSlots.end()) {
        std::cerr << "controller test did not allocate a slot\n";
        return false;
    }
    // TL covers the complete velocity-curve/channel-volume calculation; the
    // high byte of register 11 covers MIDI pan conversion and override.
    if ((lleSlot->registers[6] & 0xFF) !=
            (hleSlot->registers[6] & 0xFF) ||
        (lleSlot->registers[11] & 0xFF00) !=
            (hleSlot->registers[11] & 0xFF00)) {
        std::cerr << "controller-derived SCSP fields differ: LLE TL/pan=0x"
                  << std::hex << (lleSlot->registers[6] & 0xFF) << "/0x"
                  << (lleSlot->registers[11] >> 8) << " HLE=0x"
                  << (hleSlot->registers[6] & 0xFF) << "/0x"
                  << (hleSlot->registers[11] >> 8) << std::dec << '\n';
        return false;
    }

    for (std::uint8_t bend : {std::uint8_t{0x70}, std::uint8_t{0x10}}) {
        if (!lle.SendMidi(0, 0x1E, 0xE0, 0, bend, error) ||
            !hle.SendMidi(0, 0x1E, 0xE0, 0, bend, error)) {
            std::cerr << "pitch-bend MIDI failed: " << error << '\n';
            return false;
        }
        lle.Render(256);
        hle.Render(256);
        const auto currentLle = lle.DebugSlots();
        const auto currentHle = hle.DebugSlots();
        const auto bentLle = find(currentLle);
        const auto bentHle = find(currentHle);
        if (bentLle == currentLle.end() || bentHle == currentHle.end() ||
            bentLle->registers[8] != bentHle->registers[8]) {
            std::cerr << "pitch bend 0x" << std::hex
                      << static_cast<unsigned>(bend)
                      << " differs in OCT/FNS: LLE=0x"
                      << (bentLle == currentLle.end()
                              ? 0
                              : bentLle->registers[8])
                      << " HLE=0x"
                      << (bentHle == currentHle.end()
                              ? 0
                              : bentHle->registers[8])
                      << std::dec << '\n';
            return false;
        }
    }

    for (const auto [controller, value] :
         {std::pair<std::uint8_t, std::uint8_t>{0x07, 48},
          std::pair<std::uint8_t, std::uint8_t>{0x0B, 63}}) {
        if (!lle.SendMidi(0, 0x1E, 0xB0, controller, value, error) ||
            !hle.SendMidi(0, 0x1E, 0xB0, controller, value, error)) {
            std::cerr << "live-volume MIDI failed: " << error << '\n';
            return false;
        }
        lle.Render(256);
        hle.Render(256);
        const auto currentLle = lle.DebugSlots();
        const auto currentHle = hle.DebugSlots();
        const auto volumeLle = find(currentLle);
        const auto volumeHle = find(currentHle);
        if (volumeLle == currentLle.end() ||
            volumeHle == currentHle.end() ||
            (volumeLle->registers[6] & 0xFF) !=
                (volumeHle->registers[6] & 0xFF)) {
            std::cerr << "live controller 0x" << std::hex
                      << static_cast<unsigned>(controller)
                      << " differs in TL: LLE=0x"
                      << (volumeLle == currentLle.end()
                              ? 0
                              : volumeLle->registers[6] & 0xFF)
                      << " HLE=0x"
                      << (volumeHle == currentHle.end()
                              ? 0
                              : volumeHle->registers[6] & 0xFF)
                      << std::dec << '\n';
            return false;
        }
    }
    if (!lle.SetSequenceVolume(0, 100, 0, error) ||
        !hle.SetSequenceVolume(0, 100, 0, error)) {
        std::cerr << "live sequence-volume command failed: " << error << '\n';
        return false;
    }
    lle.Render(256);
    hle.Render(256);
    {
        const auto currentLle = lle.DebugSlots();
        const auto currentHle = hle.DebugSlots();
        const auto volumeLle = find(currentLle);
        const auto volumeHle = find(currentHle);
        if (volumeLle == currentLle.end() ||
            volumeHle == currentHle.end() ||
            (volumeLle->registers[6] & 0xFF) !=
                (volumeHle->registers[6] & 0xFF)) {
            std::cerr << "live sequence volume differs in TL\n";
            return false;
        }
    }

    const std::uint32_t releasedLayer = lleSlot->layerAddress;
    auto noteOff = [&](saturn_sfx::SoundSystem& sound) {
        return sound.SendMidi(0, 0x1E, 0x80, 24, 0, error);
    };
    if (!noteOff(lle) || !noteOff(hle)) {
        std::cerr << "note-off test MIDI failed: " << error << '\n';
        return false;
    }
    lle.Render(256);
    hle.Render(256);
    const auto checkReleased = [&](const saturn_sfx::SoundSystem& sound,
                                   const char* backend) {
        for (const auto& slot : sound.DebugSlots()) {
            if (slot.layerAddress != releasedLayer)
                continue;
            if (slot.active || (slot.registers[0] & 0x0800) != 0) {
                std::cerr << backend
                          << " did not clear note-on state and KYONB\n";
                return false;
            }
            return true;
        }
        std::cerr << backend << " lost released slot state\n";
        return false;
    };
    if (!checkReleased(lle, "LLE") || !checkReleased(hle, "HLE"))
        return false;

    if (!lle.SendMidi(0, 0x1E, 0x90, 24, 37, error) ||
        !hle.SendMidi(0, 0x1E, 0x90, 24, 37, error)) {
        std::cerr << "slot-reuse MIDI failed: " << error << '\n';
        return false;
    }
    lle.Render(256);
    hle.Render(256);
    const auto activeCount = [](const saturn_sfx::SoundSystem& sound) {
        return std::ranges::count_if(sound.DebugSlots(), [](const auto& slot) {
            return slot.active;
        });
    };
    if (activeCount(lle) != activeCount(hle) || activeCount(hle) == 0) {
        std::cerr << "slot reuse differs after note-off\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::filesystem::path root = SATURN_SNDLIB_SOTN_GAME_DIR;
    const auto game = Read(root / "0.BIN");
    const auto crt = Read(root / "SD/SD_JO10.CRT");
    if (game.size() < kDriverOffset + kDriverSize || crt.empty())
        return 1;

    std::string error;
    saturn_sfx::SoundSystem layout;
    if (!layout.Boot(std::span(game).subspan(kDriverOffset, kDriverSize),
                     std::span(game).subspan(kAreaMapOffset, kAreaMapSize),
                     error))
        return 1;
    const auto sequenceArea = layout.FindArea(0x10);
    const auto toneArea = layout.FindArea(0x00);
    if (!sequenceArea || !toneArea || toneArea->address < sequenceArea->address)
        return 1;
    const std::size_t toneOffset = toneArea->address - sequenceArea->address;
    if (toneOffset >= crt.size())
        return 1;
    auto tone = saturn_sfx::ToneBank::Open(
        std::span(crt).subspan(toneOffset), toneArea->address, error);
    if (!tone) {
        std::cerr << error << '\n';
        return 1;
    }

    unsigned compared = 0;
    bool enough = false;
    for (std::uint8_t program = 0; program < tone->VoiceCount(); ++program) {
        for (std::uint8_t note : {std::uint8_t{24}, std::uint8_t{60},
                                  std::uint8_t{96}}) {
            error.clear();
            if (tone->SelectLayers(program, note, error).empty())
                continue;
            if (!CompareCase(game, crt, *tone, program, note))
                return 1;
            if (++compared == 12) {
                enough = true;
                break;
            }
        }
        if (enough)
            break;
    }
    if (!enough) {
        std::cerr << "only found " << compared << " comparable tone cases\n";
        return 1;
    }
    return CompareControllers(game, crt) ? 0 : 1;
}
