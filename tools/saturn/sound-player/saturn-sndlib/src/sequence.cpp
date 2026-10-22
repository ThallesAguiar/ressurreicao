#include <saturn_sndlib/sequence.hpp>

#include <algorithm>
#include <limits>
#include <vector>

namespace saturn_sfx {

namespace {

std::uint16_t Read16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) << 8 | data[offset + 1];
}

std::uint32_t Read32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) << 24 |
           static_cast<std::uint32_t>(data[offset + 1]) << 16 |
           static_cast<std::uint32_t>(data[offset + 2]) << 8 |
           data[offset + 3];
}

bool Has(std::span<const std::uint8_t> data, std::size_t offset,
         std::size_t size) {
    return offset <= data.size() && size <= data.size() - offset;
}

void Write16(
    std::span<std::uint8_t> data, std::size_t offset, std::uint16_t value) {
    data[offset] = value >> 8;
    data[offset + 1] = value;
}

void Write32(
    std::span<std::uint8_t> data, std::size_t offset, std::uint32_t value) {
    Write16(data, offset, value >> 16);
    Write16(data, offset + 2, value);
}

bool PackEvent(
    const SequenceEvent& event, std::vector<std::uint8_t>& track,
    std::string& error) {
    const std::uint8_t kind = event.status & 0xF0;
    const std::uint8_t channel = event.status & 0x0F;
    if (kind == 0x90) {
        if (event.delta > 0x1FF || event.gate > 0x1FF ||
            event.data1 > 0x7F || event.data2 > 0x7F || event.port > 1) {
            error = "sequence note event is out of packer range";
            return false;
        }
        track.push_back(static_cast<std::uint8_t>(
            channel | (event.port << 4) |
            (event.delta & 0x100 ? 0x20 : 0) |
            (event.gate & 0x100 ? 0x40 : 0)));
        track.push_back(event.data1);
        track.push_back(event.data2);
        track.push_back(event.gate);
        track.push_back(event.delta);
        return true;
    }
    if (kind < 0xA0 || kind > 0xE0 || event.delta > 0xFF ||
        event.data1 > 0x7F || event.data2 > 0x7F || event.port > 1) {
        error = "sequence MIDI event is unsupported by the packer";
        return false;
    }
    track.push_back(event.status);
    if (kind == 0xE0) {
        track.push_back(event.data2);
    } else {
        track.push_back(static_cast<std::uint8_t>(
            event.data1 | (event.port << 7)));
        if (kind != 0xC0 && kind != 0xD0)
            track.push_back(event.data2);
    }
    track.push_back(event.delta);
    return true;
}

} // namespace

std::optional<std::vector<std::uint8_t>> PackSequenceBank(
    std::span<const SequenceSong> songs, std::string& error) {
    if (songs.empty() || songs.size() > 256) {
        error = "sequence bank must contain 1..256 songs";
        return std::nullopt;
    }
    std::vector<std::vector<std::uint8_t>> packedSongs;
    packedSongs.reserve(songs.size());
    for (const auto& song : songs) {
        if (song.resolution < 24 || song.resolution > 960 ||
            song.microsecondsPerQuarter < 200000 ||
            song.microsecondsPerQuarter > 1500000) {
            error = "sequence song timing is outside the driver range";
            return std::nullopt;
        }
        std::vector<std::uint8_t> track;
        for (const auto& event : song.events) {
            if (!PackEvent(event, track, error))
                return std::nullopt;
        }
        track.push_back(0x83);
        std::vector<std::uint8_t> packed(16 + track.size());
        Write16(packed, 0, song.resolution);
        Write16(packed, 2, 1);
        Write16(packed, 4, 16);
        Write16(packed, 6, 8);
        Write32(packed, 8, 0);
        Write32(packed, 12, song.microsecondsPerQuarter);
        std::ranges::copy(track, packed.begin() + 16);
        packedSongs.push_back(std::move(packed));
    }

    const std::size_t headerSize = 2 + songs.size() * 4;
    std::size_t size = headerSize;
    for (const auto& song : packedSongs)
        size += song.size();
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        error = "sequence bank is too large";
        return std::nullopt;
    }
    std::vector<std::uint8_t> bank(size);
    Write16(bank, 0, static_cast<std::uint16_t>(songs.size()));
    std::size_t offset = headerSize;
    for (std::size_t i = 0; i < packedSongs.size(); ++i) {
        Write32(bank, 2 + i * 4, static_cast<std::uint32_t>(offset));
        std::ranges::copy(packedSongs[i], bank.begin() + offset);
        offset += packedSongs[i].size();
    }
    return bank;
}

std::optional<SequenceDecoder> SequenceDecoder::Open(
    std::span<const std::uint8_t> bank, std::uint8_t song,
    std::string& error) {
    if (!Has(bank, 0, 2)) {
        error = "sequence bank header is truncated";
        return std::nullopt;
    }
    const std::uint16_t songCount = Read16(bank, 0);
    if (song >= songCount || !Has(bank, 2 + song * 4, 4)) {
        error = "sequence song is not present in this bank";
        return std::nullopt;
    }
    const std::uint32_t songOffset = Read32(bank, 2 + song * 4);
    if (!Has(bank, songOffset, 16)) {
        error = "sequence song header points outside the bank";
        return std::nullopt;
    }

    SequenceDecoder decoder;
    decoder.m_song = bank.subspan(songOffset);
    decoder.m_resolution = Read16(decoder.m_song, 0);
    const std::uint16_t trackOffset = Read16(decoder.m_song, 4);
    const std::uint16_t tempoOffset = Read16(decoder.m_song, 6);
    if (decoder.m_resolution < 24 || decoder.m_resolution > 960 ||
        !Has(decoder.m_song, trackOffset, 1) ||
        !Has(decoder.m_song, tempoOffset, 8)) {
        error = "invalid sequence song header";
        return std::nullopt;
    }
    decoder.m_track = trackOffset;
    decoder.m_cursor = trackOffset;
    decoder.m_tempo = {
        Read32(decoder.m_song, tempoOffset),
        Read32(decoder.m_song, tempoOffset + 4),
    };
    if (decoder.m_tempo.microsecondsPerQuarter < 200000 ||
        decoder.m_tempo.microsecondsPerQuarter > 1500000) {
        error = "initial sequence tempo is outside the driver range";
        return std::nullopt;
    }
    return decoder;
}

std::optional<SequenceEvent> SequenceDecoder::Next(std::string& error) {
    if (m_ended)
        return std::nullopt;

    m_lastLoopDelta.reset();
    SequenceEvent result;
    for (std::size_t operations = 0; operations < 65536; ++operations) {
        if (!Has(m_song, m_cursor, 1)) {
            error = "sequence event stream is truncated";
            return std::nullopt;
        }
        const std::uint8_t command = m_song[m_cursor++];
        if (command <= 0x7F) {
            if (!Has(m_song, m_cursor, 4)) {
                error = "sequence note event is truncated";
                return std::nullopt;
            }
            result.status = static_cast<std::uint8_t>(0x90 | (command & 0x0F));
            result.port = command >> 4 & 1;
            result.gate += (command & 0x40) ? 0x100 : 0;
            result.delta += (command & 0x20) ? 0x100 : 0;
            result.data1 = m_song[m_cursor++];
            result.data2 = m_song[m_cursor++];
            result.gate += m_song[m_cursor++];
            result.delta += m_song[m_cursor++];
        } else if (command == 0x81) {
            if (!Has(m_song, m_cursor, 3)) {
                error = "sequence reference event is truncated";
                return std::nullopt;
            }
            const std::uint16_t target = Read16(m_song, m_cursor);
            const std::uint8_t count = m_song[m_cursor + 2];
            m_cursor += 3;
            if (!Has(m_song, m_track + target, 1) || count == 0) {
                error = "invalid sequence reference event";
                return std::nullopt;
            }
            m_return = m_cursor;
            m_referenceCount = count;
            m_cursor = m_track + target;
            continue;
        } else if (command == 0x82) {
            if (!Has(m_song, m_cursor, 1)) {
                error = "sequence loop event is truncated";
                return std::nullopt;
            }
            result.delta += m_song[m_cursor++];
            if (m_loop == 0)
                m_loop = m_cursor;
            else {
                ++m_loopCount;
                m_lastLoopDelta = result.delta;
                m_cursor = m_loop;
            }
            continue;
        } else if (command == 0x83) {
            m_ended = true;
            return std::nullopt;
        } else if (command >= 0x88 && command <= 0x8B) {
            constexpr std::uint32_t values[]{0x200, 0x800, 0x1000, 0x2000};
            result.gate += values[command - 0x88];
            continue;
        } else if (command >= 0x8C && command <= 0x8F) {
            constexpr std::uint32_t values[]{0x100, 0x200, 0x800, 0x1000};
            result.delta += values[command - 0x8C];
            continue;
        } else if ((command & 0xF0) >= 0xA0 &&
                   (command & 0xF0) <= 0xE0) {
            const std::uint8_t kind = command & 0xF0;
            const std::size_t parameterCount =
                kind == 0xC0 || kind == 0xD0 || kind == 0xE0 ? 1 : 2;
            if (!Has(m_song, m_cursor, parameterCount + 1)) {
                error = "sequence MIDI event is truncated";
                return std::nullopt;
            }
            result.status = command;
            if (kind == 0xE0) {
                result.data2 = m_song[m_cursor++];
            } else {
                result.data1 = m_song[m_cursor++];
                if (parameterCount == 2)
                    result.data2 = m_song[m_cursor++];
            }
            result.port = result.data1 >> 7;
            result.data1 &= 0x7F;
            result.delta += m_song[m_cursor++];
        } else {
            error = "unsupported sequence event 0x";
            constexpr char hex[] = "0123456789ABCDEF";
            error += hex[command >> 4];
            error += hex[command & 0xF];
            return std::nullopt;
        }

        if (m_referenceCount != 0 && --m_referenceCount == 0)
            m_cursor = m_return;
        return result;
    }
    error = "sequence decoder operation limit exceeded";
    return std::nullopt;
}

} // namespace saturn_sfx
