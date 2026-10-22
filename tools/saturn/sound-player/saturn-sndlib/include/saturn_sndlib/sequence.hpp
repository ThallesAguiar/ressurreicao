#pragma once

// Standalone Saturn SNDLIB emulation API.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace saturn_sfx {

struct SequenceEvent {
    std::uint32_t delta = 0;
    std::uint32_t gate = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint8_t port = 0;

    bool operator==(const SequenceEvent&) const = default;
};

struct SequenceTempo {
    std::uint32_t delta = 0;
    std::uint32_t microsecondsPerQuarter = 0;
};

struct SequenceSong {
    std::uint16_t resolution = 480;
    std::uint32_t microsecondsPerQuarter = 500000;
    std::vector<SequenceEvent> events;
};

std::optional<std::vector<std::uint8_t>> PackSequenceBank(
    std::span<const SequenceSong> songs, std::string& error);

class SequenceDecoder {
  public:
    static std::optional<SequenceDecoder> Open(
        std::span<const std::uint8_t> bank, std::uint8_t song,
        std::string& error);

    std::uint16_t Resolution() const { return m_resolution; }
    const SequenceTempo& InitialTempo() const { return m_tempo; }
    std::uint32_t LoopCount() const { return m_loopCount; }
    const std::optional<std::uint32_t>& LastLoopDelta() const {
        return m_lastLoopDelta;
    }
    std::optional<SequenceEvent> Next(std::string& error);

  private:
    std::span<const std::uint8_t> m_song;
    std::size_t m_track = 0;
    std::size_t m_cursor = 0;
    std::size_t m_loop = 0;
    std::size_t m_return = 0;
    std::uint8_t m_referenceCount = 0;
    std::uint16_t m_resolution = 0;
    SequenceTempo m_tempo;
    std::uint32_t m_loopCount = 0;
    std::optional<std::uint32_t> m_lastLoopDelta;
    bool m_ended = false;
};

} // namespace saturn_sfx
