#pragma once

#include <saturn_sndlib/sequence.hpp>
#include <saturn_sndlib/sound_system.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace saturn_sfx {

class HleSequencePlayer {
  public:
    static std::optional<HleSequencePlayer> Create(
        std::span<const std::uint8_t> sequenceBank, std::uint8_t song,
        std::uint8_t slot, std::uint8_t priority, std::string& error);

    bool Render(SoundSystem& sound, std::size_t sampleCount,
                std::string& error);

    bool Finished() const { return !m_next && m_noteOffs.empty(); }
    const std::optional<std::uint64_t>& FirstLoopSample() const {
        return m_firstLoopSample;
    }

  private:
    explicit HleSequencePlayer(SequenceDecoder decoder)
        : m_decoder(std::move(decoder)) {}

    struct NoteOff {
        std::uint64_t tick;
        std::uint8_t status;
        std::uint8_t note;
    };

    bool LoadNext(std::string& error);
    std::uint64_t TickToSample(std::uint64_t tick) const;

    SequenceDecoder m_decoder;
    std::optional<SequenceEvent> m_next;
    std::vector<NoteOff> m_noteOffs;
    std::uint64_t m_nextTick = 0;
    std::uint64_t m_renderedSamples = 0;
    std::optional<std::uint64_t> m_firstLoopSample;
    std::uint8_t m_slot = 0;
    std::uint8_t m_priority = 0;
};

} // namespace saturn_sfx
