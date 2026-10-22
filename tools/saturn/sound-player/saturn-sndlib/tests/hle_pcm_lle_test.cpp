#include <saturn_sndlib/hle_sequence_player.hpp>
#include <saturn_sndlib/sound_system.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <optional>
#include <vector>

namespace {

constexpr std::size_t kDriverOffset = 0x369FC;
constexpr std::size_t kDriverSize = 0x67F2;
constexpr std::size_t kAreaMapOffset = 0x369C0;
constexpr std::size_t kAreaMapSize = 58;
constexpr std::size_t kRenderFrames = 44100;

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::size_t FirstNonzero(
    const std::vector<saturn_sfx::StereoSample>& samples) {
    const auto it = std::ranges::find_if(samples, [](const auto& sample) {
        return sample.left != 0 || sample.right != 0;
    });
    return static_cast<std::size_t>(it - samples.begin());
}

bool CheckCase(
    std::span<const std::uint8_t> driver,
    std::span<const std::uint8_t> map,
    const std::vector<std::uint8_t>& common,
    const std::vector<std::uint8_t>& package, std::uint8_t packageArea,
    std::size_t sequenceOffset, std::uint8_t bank, std::uint8_t song,
    std::uint8_t priority, std::size_t renderFrames,
    std::size_t minimumIdentical, const char* label) {
    std::string error;
    saturn_sfx::SoundSystem lle;
    saturn_sfx::SoundSystem hle;
    if (!lle.Boot(driver, map, error) || !hle.BootHle(map, error) ||
        !lle.LoadArea(0x10, common, error) ||
        !hle.LoadArea(0x10, common, error) ||
        (packageArea != 0x10 && !lle.LoadArea(packageArea, package, error)) ||
        (packageArea != 0x10 && !hle.LoadArea(packageArea, package, error)) ||
        !lle.StartSequence(0, bank, song, priority, error) ||
        !lle.SetSequenceVolume(0, 130, 0, error) ||
        !hle.SetSequenceVolume(0, 130, 0, error)) {
        std::cerr << label << " PCM oracle setup failed: " << error << '\n';
        return false;
    }
    auto player = saturn_sfx::HleSequencePlayer::Create(
        std::span(package).subspan(sequenceOffset), song, 0, priority, error);
    if (!player) {
        std::cerr << label << " HLE sequence setup failed: " << error << '\n';
        return false;
    }

    lle.ClearSamples();
    hle.ClearSamples();
    std::optional<saturn_sfx::SlotDebugState> firstLleSlot;
    std::optional<saturn_sfx::SlotDebugState> firstHleSlot;
    for (std::size_t rendered = 0; rendered < renderFrames;) {
        const std::size_t chunk = std::min<std::size_t>(64, renderFrames - rendered);
        lle.Render(chunk);
        if (!player->Render(hle, chunk, error)) {
            std::cerr << label << " HLE PCM render failed: " << error << '\n';
            return false;
        }
        const auto capture = [](const saturn_sfx::SoundSystem& sound)
            -> std::optional<saturn_sfx::SlotDebugState> {
            const auto slots = sound.DebugSlots();
            const auto active = std::ranges::find_if(
                slots, [](const auto& slot) { return slot.active; });
            if (active == slots.end())
                return std::nullopt;
            return *active;
        };
        if (!firstLleSlot)
            firstLleSlot = capture(lle);
        if (!firstHleSlot)
            firstHleSlot = capture(hle);
        rendered += chunk;
    }
    if (!firstLleSlot || !firstHleSlot) {
        std::cerr << label << " did not expose an active voice\n";
        return false;
    }
    if (firstLleSlot->layerAddress != firstHleSlot->layerAddress ||
        firstLleSlot->registers != firstHleSlot->registers) {
        std::cerr << label << " first voice differs: layer LLE=0x" << std::hex
                  << firstLleSlot->layerAddress << " HLE=0x"
                  << firstHleSlot->layerAddress << std::dec << '\n';
        for (std::size_t reg = 0; reg < firstLleSlot->registers.size(); ++reg) {
            if (firstLleSlot->registers[reg] != firstHleSlot->registers[reg])
                std::cerr << "  reg " << reg << " LLE=0x" << std::hex
                          << firstLleSlot->registers[reg] << " HLE=0x"
                          << firstHleSlot->registers[reg] << std::dec << '\n';
        }
        return false;
    }
    const auto& llePcm = lle.Samples();
    const auto& hlePcm = hle.Samples();
    const std::size_t lleStart = FirstNonzero(llePcm);
    const std::size_t hleStart = FirstNonzero(hlePcm);
    if (lleStart == llePcm.size() || hleStart == hlePcm.size() ||
        std::max(lleStart, hleStart) - std::min(lleStart, hleStart) > 256) {
        std::cerr << label << " LLE/HLE PCM onset is missing or too far apart\n";
        return false;
    }

    const std::size_t comparable =
        std::min(llePcm.size() - lleStart, hlePcm.size() - hleStart);
    std::size_t identical = 0;
    for (; identical < comparable; ++identical) {
        const auto& a = llePcm[lleStart + identical];
        const auto& b = hlePcm[hleStart + identical];
        if (a.left != b.left || a.right != b.right)
            break;
    }
    if (identical < minimumIdentical) {
        std::cerr << label << " only has " << identical
                  << " aligned PCM frames matching the 68000 driver"
                  << " (onsets LLE=" << lleStart << " HLE=" << hleStart
                  << ", first LLE=" << llePcm[lleStart].left << ','
                  << llePcm[lleStart].right << " HLE="
                  << hlePcm[hleStart].left << ',' << hlePcm[hleStart].right
                  << ")\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::filesystem::path root = SATURN_SNDLIB_SOTN_GAME_DIR;
    const auto game = Read(root / "0.BIN");
    const auto common = Read(root / "SD/SD_JO10.CRT");
    const auto stage = Read(root / "SD/SD_ROU.CRT");
    if (game.size() < kDriverOffset + kDriverSize || common.empty() ||
        stage.empty())
        return 1;
    const auto driver = std::span(game).subspan(kDriverOffset, kDriverSize);
    const auto map = std::span(game).subspan(kAreaMapOffset, kAreaMapSize);

    return CheckCase(driver, map, common, common, 0x10, 0, 0, 0, 30,
                     kRenderFrames, 4000, "0x601") &&
                   CheckCase(driver, map, common, stage, 0x12, 0x800, 3,
                             113, 50, kRenderFrames * 2, 0, "0x7A1")
               ? 0
               : 1;
}
