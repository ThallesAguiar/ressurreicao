#include "scorcher_lle.hpp"

#include <array>
#include <memory>

namespace saturn_sfx::scorcher {

bool LlePlayer::Render(const MusImage& image, std::size_t sampleCount,
                       PlaybackResult& result, std::string& error,
                       bool trace) {
    // This deliberately mirrors Scorcher's dynamically loaded SDDRVS.TSK
    // vtable+0x34 method at SH-2 address 0x00200ED0: boot v1.31, use the host
    // command blocks at sound RAM 0x700, ChangeMap(0) twice, upload and mark
    // areas ready, select DSP/mixer zero, then start slot 0 at volume 127.
    // The upload helper is 0x00201B3C and the map resolver is 0x00202798.
    // Omitting the ready bits is a known silent failure mode.
    auto sound = std::make_unique<SoundSystem>(
        SegaSoundDriverVersion::V1_31);
    constexpr std::array<std::uint8_t, 1> kZero{0};
    constexpr std::array<std::uint8_t, 2> kTwoZeros{0, 0};
    if (!sound->BootImage(image.soundRam, error) ||
        !sound->ConfigureCommandInterface(0x700, error) ||
        !sound->ChangeMap(0, error) || !sound->ChangeMap(0, error)) {
        return false;
    }
    for (const auto& payload : image.payloads) {
        const std::array<std::uint8_t, 4> readyFlag{
            static_cast<std::uint8_t>(payload.readyFlagValue >> 24),
            static_cast<std::uint8_t>(payload.readyFlagValue >> 16),
            static_cast<std::uint8_t>(payload.readyFlagValue >> 8),
            static_cast<std::uint8_t>(payload.readyFlagValue),
        };
        if (!sound->LoadSoundRam(payload.address, payload.data, error) ||
            !sound->LoadSoundRam(payload.readyFlagAddress, readyFlag, error)) {
            return false;
        }
    }
    if (!sound->SendCommand(0x83, kZero, error) ||
        !sound->SendCommand(0x87, kTwoZeros, error) ||
        !sound->SetTotalVolume(127, error)) {
        return false;
    }
    if (trace)
        sound->BeginScspTrace();
    if (!sound->StartSequence(0, 0, 0, 0, error) ||
        !sound->SetSequenceVolume(0, 127, 0, error)) {
        return false;
    }
    sound->ClearSamples();
    sound->Render(sampleCount);
    result.samples = sound->Samples();
    result.scspTrace = sound->ScspTrace();
    return true;
}

} // namespace saturn_sfx::scorcher
