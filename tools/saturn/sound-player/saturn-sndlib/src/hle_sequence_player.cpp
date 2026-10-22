#include <saturn_sndlib/hle_sequence_player.hpp>

#include <algorithm>

namespace saturn_sfx {

std::optional<HleSequencePlayer> HleSequencePlayer::Create(
    std::span<const std::uint8_t> sequenceBank, std::uint8_t song,
    std::uint8_t slot, std::uint8_t priority, std::string& error) {
    auto decoder = SequenceDecoder::Open(sequenceBank, song, error);
    if (!decoder)
        return std::nullopt;
    HleSequencePlayer player{std::move(*decoder)};
    player.m_slot = slot;
    player.m_priority = priority;
    if (!player.LoadNext(error) && !error.empty())
        return std::nullopt;
    return player;
}

bool HleSequencePlayer::LoadNext(std::string& error) {
    const std::uint64_t previousTick = m_nextTick;
    m_next = m_decoder.Next(error);
    if (!m_firstLoopSample && m_decoder.LastLoopDelta()) {
        m_firstLoopSample =
            TickToSample(previousTick + *m_decoder.LastLoopDelta());
    }
    if (m_next)
        m_nextTick += m_next->delta;
    return m_next.has_value();
}

std::uint64_t HleSequencePlayer::TickToSample(std::uint64_t tick) const {
    const std::uint64_t numerator =
        tick * m_decoder.InitialTempo().microsecondsPerQuarter * 44100ULL;
    return numerator / (static_cast<std::uint64_t>(m_decoder.Resolution()) *
                        1000000ULL);
}

bool HleSequencePlayer::Render(
    SoundSystem& sound, std::size_t sampleCount, std::string& error) {
    constexpr std::size_t kMaximumEventsPerRender = 4096;
    std::size_t dispatchedEvents = 0;
    const std::size_t outputStart = sound.Samples().size();
    const std::uint64_t endSample = m_renderedSamples + sampleCount;
    auto currentSample = [&]() {
        return m_renderedSamples + sound.Samples().size() - outputStart;
    };
    while (currentSample() < endSample) {
        std::uint64_t due = endSample;
        if (m_next)
            due = std::min(due, TickToSample(m_nextTick));
        for (const auto& off : m_noteOffs)
            due = std::min(due, TickToSample(off.tick));

        if (currentSample() < due)
            sound.Render(due - currentSample());

        const std::uint64_t now = currentSample();
        for (auto it = m_noteOffs.begin(); it != m_noteOffs.end();) {
            if (TickToSample(it->tick) <= now) {
                if (!sound.SendMidi(m_slot, m_priority, it->status, it->note,
                                    0, error))
                    return false;
                it = m_noteOffs.erase(it);
            } else {
                ++it;
            }
        }

        while (m_next &&
               TickToSample(m_nextTick) <= currentSample()) {
            if (++dispatchedEvents > kMaximumEventsPerRender) {
                error = "HLE sequence dispatched over 4096 events without "
                        "advancing an audio chunk";
                return false;
            }
            const SequenceEvent event = *m_next;
            if (!sound.SendMidi(m_slot, m_priority, event.status, event.data1,
                                event.data2, error))
                return false;
            if ((event.status & 0xF0) == 0x90 && event.data2 != 0 &&
                event.gate != 0) {
                m_noteOffs.push_back(
                    {m_nextTick + event.gate,
                     static_cast<std::uint8_t>(0x80 | (event.status & 0x0F)),
                     event.data1});
            }
            if (!LoadNext(error) && !error.empty())
                return false;
        }

        if (due == endSample && currentSample() < endSample)
            sound.Render(endSample - currentSample());
    }
    sound.TrimSamples(outputStart + sampleCount);
    m_renderedSamples = endSample;
    return true;
}

} // namespace saturn_sfx
