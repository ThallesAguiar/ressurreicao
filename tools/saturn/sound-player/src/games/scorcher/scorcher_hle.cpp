#include "scorcher_hle.hpp"

#include <saturn_sndlib/hle_sequence_player.hpp>

#include <algorithm>
#include <memory>
#include <optional>

namespace saturn_sfx::scorcher {

namespace {

std::uint16_t Read16(std::span<const std::uint8_t> data,
                     std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) << 8 | data[offset + 1];
}

bool Prepare(const MusImage& image, SoundSystem& sound,
             std::optional<HleSequencePlayer>& player, std::string& error) {
    if (!sound.BootHle(image.areaMap, error))
        return false;

    const Payload* sequence = nullptr;
    const Payload* tones = nullptr;
    const Payload* effect = nullptr;
    for (const auto& payload : image.payloads) {
        if (!sound.LoadArea(payload.id, payload.data, error))
            return false;
        if (payload.id == 0x10)
            sequence = &payload;
        else if (payload.id == 0x00)
            tones = &payload;
        else if (payload.id == 0x20)
            effect = &payload;
    }
    if (sequence == nullptr) {
        error = "Scorcher MUS file has no sequence payload";
        return false;
    }
    if (tones == nullptr || effect == nullptr || tones->data.size() < 2) {
        error = "Scorcher MUS file has incomplete tone or effect payloads";
        return false;
    }
    // The tone-bank's first big-endian word points at mixer zero; SDDRVS.TSK
    // command 0x87 copies its first 16 bytes to the low byte of SCSP slot
    // register 0x16 for effect outputs 0..15. Command 0x83 consumes the EXB
    // payload and uses map area 0x30 as DSP work RAM. These layouts were
    // checked against the v1.31 LLE register trace. Recheck changes with
    // scorcher_lle_hle_test across LEVEL1..LEVEL6 and TITLE.
    const std::size_t mixerOffset = Read16(tones->data, 0);
    const auto dspRam = sound.FindArea(0x30);
    if (mixerOffset > tones->data.size() ||
        16 > tones->data.size() - mixerOffset || !dspRam ||
        !sound.LoadDspProgram(
            effect->data, dspRam->address, dspRam->size, error) ||
        !sound.SetEffectMixer(
            std::span(tones->data).subspan(mixerOffset, 16), error)) {
        if (error.empty())
            error = "Scorcher MUS mixer or DSP work area is invalid";
        return false;
    }

    player = HleSequencePlayer::Create(sequence->data, 0, 0, 0, error);
    return player && sound.SetTotalVolume(127, error) &&
           sound.SetSequenceVolume(0, 127, 0, error);
}

} // namespace

bool HlePlayer::Render(const MusImage& image, std::size_t sampleCount,
                       PlaybackResult& result, std::string& error,
                       bool trace) {
    auto sound = std::make_unique<SoundSystem>(
        SegaSoundDriverVersion::V1_31);
    std::optional<HleSequencePlayer> player;
    if (!Prepare(image, *sound, player, error)) {
        return false;
    }
    if (trace)
        sound->BeginScspTrace();
    sound->ClearSamples();
    if (!player->Render(*sound, sampleCount, error))
        return false;
    result.samples = sound->Samples();
    result.scspTrace = sound->ScspTrace();
    return true;
}

bool HlePlayer::RenderThroughFirstLoop(
    const MusImage& image, std::size_t trailingSamples,
    std::size_t maximumSamples, PlaybackResult& result,
    std::string& error, bool trace) {
    auto sound = std::make_unique<SoundSystem>(
        SegaSoundDriverVersion::V1_31);
    std::optional<HleSequencePlayer> player;
    if (!Prepare(image, *sound, player, error))
        return false;
    if (trace)
        sound->BeginScspTrace();
    sound->ClearSamples();

    constexpr std::size_t kChunkSamples = 4096;
    std::size_t rendered = 0;
    while (!player->FirstLoopSample()) {
        if (rendered >= maximumSamples) {
            error = "Scorcher sequence did not loop before safety limit";
            return false;
        }
        const std::size_t chunk =
            std::min(kChunkSamples, maximumSamples - rendered);
        if (!player->Render(*sound, chunk, error))
            return false;
        rendered += chunk;
    }

    const std::uint64_t target = *player->FirstLoopSample() + trailingSamples;
    if (target > maximumSamples) {
        error = "Scorcher loop verification exceeds safety limit";
        return false;
    }
    if (rendered < target &&
        !player->Render(*sound, target - rendered, error)) {
        return false;
    }
    sound->TrimSamples(target);
    result.samples = sound->Samples();
    result.scspTrace = sound->ScspTrace();
    return true;
}

} // namespace saturn_sfx::scorcher
