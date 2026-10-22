#pragma once

#include <saturn_sndlib/driver_version.hpp>

// Standalone Saturn SNDLIB emulation API.

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace saturn_sfx {

struct ToneLayer {
    std::size_t offset;
    std::uint8_t startNote;
    std::uint8_t endNote;
    std::uint32_t sourceAddress;
    std::uint16_t loopStart;
    std::uint16_t loopEnd;
    std::uint32_t envelope;
    std::uint8_t soundDirect;
    std::uint8_t totalLevel;
    std::uint16_t modulation;
    std::uint16_t lfo;
    std::uint16_t effectInput;
    std::uint8_t directPan;
    std::uint8_t baseNote;
    std::int8_t fineTune;
    std::uint8_t velocityCurve;
};

struct ToneVoice {
    std::size_t offset;
    std::uint8_t playModeAndBendRange;
    std::uint8_t portamentoTime;
    std::int8_t layerCountMinusOne;
    std::int8_t volumeBias;
    std::vector<ToneLayer> layers;
};

struct ScspSlotImage {
    std::array<std::uint16_t, 16> registers{};
    // Some slot fields (volume, pitch, and mixer output) also depend on MIDI
    // controller state. The mask identifies the bits derived solely from the
    // tone layer and therefore suitable for direct differential comparison.
    std::array<std::uint16_t, 16> toneMask{};
};

struct PcmTone {
    std::vector<std::int16_t> samples;
    std::uint8_t startNote = 0;
    std::uint8_t endNote = 127;
    std::uint8_t baseNote = 69;
    std::int8_t fineTune = 0;
    std::uint8_t totalLevel = 0x10;
    std::uint8_t directPan = 0xF0;
    std::uint32_t envelope = 0x001F001F;
    std::uint8_t bendRange = 2;
};

std::optional<std::vector<std::uint8_t>> PackPcmToneBank(
    std::span<const PcmTone> programs, std::string& error);

ScspSlotImage BuildScspSlotImage(
    const ToneLayer& layer, std::uint32_t toneBankAddress,
    std::uint8_t note, std::int16_t pitchTune = 0,
    SegaSoundDriverVersion driverVersion =
        SegaSoundDriverVersion::V2_10L);

std::uint16_t ComputeScspPitch(std::uint8_t baseNote, std::int8_t fineTune,
                               std::uint8_t note, std::int16_t pitchTune);
std::uint16_t ComputeScspPitch(
    SegaSoundDriverVersion driverVersion, std::uint8_t baseNote,
    std::int8_t fineTune, std::uint8_t note, std::int16_t pitchTune);

class ToneBank {
  public:
    static std::optional<ToneBank> Open(
        std::span<const std::uint8_t> data, std::uint32_t soundAddress,
        std::string& error);

    std::size_t VoiceCount() const { return m_voiceOffsets.size(); }
    std::optional<ToneVoice> Voice(std::uint8_t program,
                                   std::string& error) const;
    std::vector<ToneLayer> SelectLayers(
        std::uint8_t program, std::uint8_t note, std::string& error) const;
    std::optional<std::uint8_t> TotalLevel(
        std::uint8_t program, const ToneLayer& layer, std::uint8_t velocity,
        std::uint8_t midiVolume, std::uint8_t sequenceVolume,
        std::string& error, std::uint16_t* slotVelocity = nullptr,
        SegaSoundDriverVersion driverVersion =
            SegaSoundDriverVersion::V2_10L) const;
    std::uint32_t SoundAddress() const { return m_soundAddress; }

  private:
    std::span<const std::uint8_t> m_data;
    std::uint32_t m_soundAddress = 0;
    std::vector<std::uint16_t> m_voiceOffsets;
};

} // namespace saturn_sfx
