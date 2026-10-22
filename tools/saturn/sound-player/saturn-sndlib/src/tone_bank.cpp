#include <saturn_sndlib/tone_bank.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace saturn_sfx {

namespace {

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

std::uint16_t Read16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) << 8 | data[offset + 1];
}

std::uint32_t Read32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) << 24 |
           static_cast<std::uint32_t>(data[offset + 1]) << 16 |
           static_cast<std::uint32_t>(data[offset + 2]) << 8 |
           data[offset + 3];
}

} // namespace

std::optional<std::vector<std::uint8_t>> PackPcmToneBank(
    std::span<const PcmTone> programs, std::string& error) {
    if (programs.empty() || programs.size() > 128) {
        error = "tone bank must contain 1..128 programs";
        return std::nullopt;
    }
    const std::size_t mixerOffset = 8 + programs.size() * 2;
    const std::size_t voiceOffset = mixerOffset;
    const std::size_t velocityOffset = voiceOffset + programs.size() * 0x24;
    std::size_t sampleOffset = (velocityOffset + 10 + 1) & ~std::size_t{1};
    std::size_t size = sampleOffset;
    for (const auto& program : programs) {
        if (program.samples.empty() || program.samples.size() > 65536 ||
            program.startNote > program.endNote || program.bendRange > 13 ||
            program.samples.size() >
                (std::numeric_limits<std::size_t>::max() - size) / 2) {
            error = "PCM tone program is out of packer range";
            return std::nullopt;
        }
        size += program.samples.size() * 2;
    }
    if (size > 0x00FF'FFFF) {
        error = "tone bank exceeds the 24-bit source-address range";
        return std::nullopt;
    }

    std::vector<std::uint8_t> bank(size);
    Write16(bank, 0, static_cast<std::uint16_t>(mixerOffset));
    Write16(bank, 2, static_cast<std::uint16_t>(velocityOffset));
    bank[velocityOffset] = 0x02;
    bank[velocityOffset + 1] = 127;
    for (std::size_t i = 0; i < programs.size(); ++i) {
        const auto& program = programs[i];
        const std::size_t voice = voiceOffset + i * 0x24;
        const std::size_t layer = voice + 4;
        Write16(bank, 8 + i * 2, static_cast<std::uint16_t>(voice));
        bank[voice] = program.bendRange;
        bank[voice + 2] = 0;
        bank[layer] = program.startNote;
        bank[layer + 1] = program.endNote;
        Write32(bank, layer + 2, static_cast<std::uint32_t>(sampleOffset));
        Write16(bank, layer + 6, 0);
        Write16(bank, layer + 8,
                static_cast<std::uint16_t>(program.samples.size() - 1));
        Write32(bank, layer + 0x0A, program.envelope);
        bank[layer + 0x0F] = program.totalLevel;
        bank[layer + 0x18] = program.directPan;
        bank[layer + 0x19] = program.baseNote;
        bank[layer + 0x1A] = static_cast<std::uint8_t>(program.fineTune);
        for (const auto sample : program.samples) {
            Write16(bank, sampleOffset, static_cast<std::uint16_t>(sample));
            sampleOffset += 2;
        }
    }
    return bank;
}

std::uint16_t ComputeScspPitch(
    std::uint8_t baseNote, std::int8_t fineTune, std::uint8_t note,
    std::int16_t pitchTune) {
    return ComputeScspPitch(
        SegaSoundDriverVersion::V2_10L, baseNote, fineTune, note, pitchTune);
}

std::uint16_t ComputeScspPitch(
    SegaSoundDriverVersion driverVersion, std::uint8_t baseNote,
    std::int8_t fineTune, std::uint8_t note, std::int16_t pitchTune) {
    // send_frequency is at:
    //   v1.31: LEVEL1.MUS+0x3124 (embedded driver+0x3100)
    //   v2.10-L: 0.BIN+0x3A4F2 (embedded driver+0x3AF6)
    // Their arithmetic is byte-identical. Their FNSTB tables are likewise
    // identical and begin at driver+0x36EA and driver+0x4164, respectively.
    // Keep the version in this interface so a frontend selects one coherent
    // driver policy and future revisions can supply another table.
    static_cast<void>(driverVersion);
    const std::uint16_t tune = static_cast<std::uint16_t>(
        pitchTune + static_cast<std::int16_t>(fineTune));
    const std::uint8_t relativeNote = static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(note) + 96 - (baseNote & 0x7F) +
        (tune >> 8));
    const unsigned semitone = relativeNote % 12;
    const unsigned octave = (relativeNote / 12 + 8) & 0x0F;
    const unsigned fineStep = static_cast<std::uint8_t>(tune) >> 1;
    const unsigned frequencyStep = semitone * 128 + fineStep;
    const auto fns = static_cast<unsigned>(std::floor(
        (std::exp2(static_cast<double>(frequencyStep) / 1536.0) - 1.0) *
        1024.0));
    return static_cast<std::uint16_t>(octave << 11 | fns);
}

ScspSlotImage BuildScspSlotImage(
    const ToneLayer& layer, std::uint32_t toneBankAddress,
    std::uint8_t note, std::int16_t pitchTune,
    SegaSoundDriverVersion driverVersion) {
    ScspSlotImage image;

    // The layer's SA longword contains the SCSP control byte in its high byte
    // and an address relative to the start of the tone bank in the remaining
    // bits. NON.ASM adds the bank base, writes the longword, then sets KYONB
    // and KYONEX while preserving the three format/loop control bits. KYONEX
    // is a global strobe and is clear again by the time the slot is observed.
    const std::uint32_t source = toneBankAddress + layer.sourceAddress;
    const std::uint8_t sourceControl =
        static_cast<std::uint8_t>(source >> 24);
    image.registers[0] = static_cast<std::uint16_t>(
        ((sourceControl & 0x07) | 0x08) << 8 | ((source >> 16) & 0xFF));
    image.registers[1] = static_cast<std::uint16_t>(source);
    image.registers[2] = layer.loopStart;
    image.registers[3] = layer.loopEnd;
    image.registers[4] = static_cast<std::uint16_t>(layer.envelope >> 16);
    image.registers[5] = static_cast<std::uint16_t>(layer.envelope);
    image.registers[6] = static_cast<std::uint16_t>(layer.soundDirect) << 8;
    image.registers[7] = layer.modulation;

    // The driver table contains 128 equal subdivisions per semitone. Its
    // values are floor((2^(step/1536)-1)*1024), which is the SCSP FNS field.
    // A negative fine tune borrows one semitone exactly as the 68000's
    // logical shift of the signed word does in send_frequency.
    image.registers[8] = ComputeScspPitch(
        driverVersion, layer.baseNote, layer.fineTune, note, pitchTune);
    image.registers[9] = layer.lfo & 0x7FFF; // key-on clears LFO reset
    image.registers[10] = layer.effectInput;
    image.registers[11] = static_cast<std::uint16_t>(layer.directPan) << 8;

    image.toneMask[0] = 0xFFFF;
    image.toneMask[1] = 0xFFFF;
    image.toneMask[2] = 0xFFFF;
    image.toneMask[3] = 0xFFFF;
    image.toneMask[4] = 0xFFFF;
    image.toneMask[5] = 0xFFFF;
    image.toneMask[6] = 0xFF00;
    image.toneMask[7] = 0xFFFF;
    image.toneMask[8] = 0xFFFF;
    image.toneMask[9] = 0xFFFF;
    image.toneMask[10] = 0xFFFF;
    image.toneMask[11] = 0xFF00;
    return image;
}

std::optional<ToneBank> ToneBank::Open(
    std::span<const std::uint8_t> data, std::uint32_t soundAddress,
    std::string& error) {
    if (!Has(data, 0, 10)) {
        error = "tone bank header is truncated";
        return std::nullopt;
    }
    const std::uint16_t mixerOffset = Read16(data, 0);
    if (mixerOffset < 10 || mixerOffset > 8 + 128 * 2 ||
        ((mixerOffset - 8) & 1) != 0 || !Has(data, 8, mixerOffset - 8)) {
        error = "tone bank has an invalid voice table";
        return std::nullopt;
    }

    ToneBank bank;
    bank.m_data = data;
    bank.m_soundAddress = soundAddress;
    for (std::size_t offset = 8; offset < mixerOffset; offset += 2) {
        const std::uint16_t voiceOffset = Read16(data, offset);
        if (!Has(data, voiceOffset, 4)) {
            error = "tone bank voice points outside the bank";
            return std::nullopt;
        }
        bank.m_voiceOffsets.push_back(voiceOffset);
    }
    return bank;
}

std::optional<ToneVoice> ToneBank::Voice(
    std::uint8_t program, std::string& error) const {
    if (program >= m_voiceOffsets.size()) {
        error = "tone program is not present in this bank";
        return std::nullopt;
    }
    const std::size_t voiceOffset = m_voiceOffsets[program];
    const std::int8_t layerCountMinusOne =
        static_cast<std::int8_t>(m_data[voiceOffset + 2]);
    if (layerCountMinusOne < 0) {
        error = "tone program has no layers";
        return std::nullopt;
    }
    const std::size_t layerCount =
        static_cast<std::uint8_t>(layerCountMinusOne) + 1;
    if (layerCount > 128 || !Has(m_data, voiceOffset + 4, layerCount * 0x20)) {
        error = "tone program layers are truncated";
        return std::nullopt;
    }

    ToneVoice voice{
        voiceOffset,
        m_data[voiceOffset + 0],
        m_data[voiceOffset + 1],
        layerCountMinusOne,
        static_cast<std::int8_t>(m_data[voiceOffset + 3]),
        {},
    };
    voice.layers.reserve(layerCount);
    for (std::size_t i = 0; i < layerCount; ++i) {
        const std::size_t layer = voiceOffset + 4 + i * 0x20;
        voice.layers.push_back({
            layer,
            m_data[layer + 0x00],
            m_data[layer + 0x01],
            Read32(m_data, layer + 0x02),
            Read16(m_data, layer + 0x06),
            Read16(m_data, layer + 0x08),
            Read32(m_data, layer + 0x0A),
            m_data[layer + 0x0E],
            m_data[layer + 0x0F],
            Read16(m_data, layer + 0x10),
            Read16(m_data, layer + 0x14),
            Read16(m_data, layer + 0x16),
            m_data[layer + 0x18],
            m_data[layer + 0x19],
            static_cast<std::int8_t>(m_data[layer + 0x1A]),
            m_data[layer + 0x1D],
        });
    }
    return voice;
}

std::vector<ToneLayer> ToneBank::SelectLayers(
    std::uint8_t program, std::uint8_t note, std::string& error) const {
    const auto voice = Voice(program, error);
    if (!voice)
        return {};
    std::vector<ToneLayer> selected;
    for (const ToneLayer& layer : voice->layers) {
        if (note >= layer.startNote && note <= layer.endNote)
            selected.push_back(layer);
    }
    return selected;
}

std::optional<std::uint8_t> ToneBank::TotalLevel(
    std::uint8_t program, const ToneLayer& layer, std::uint8_t velocity,
    std::uint8_t midiVolume, std::uint8_t sequenceVolume,
    std::string& error, std::uint16_t* slotVelocity,
    SegaSoundDriverVersion driverVersion) const {
    const auto voice = Voice(program, error);
    if (!voice)
        return std::nullopt;

    // FM modulators and layers with no output send retain their authored TL;
    // the driver only applies MIDI velocity to audible carriers.
    if ((layer.sourceAddress & (1u << 29)) == 0 &&
        (layer.effectInput & 7) == 0 && (layer.directPan & 0xE0) == 0) {
        if (slotVelocity)
            *slotVelocity = 0;
        return layer.totalLevel;
    }

    const std::uint16_t velocityTable = Read16(m_data, 2);
    const std::size_t curve = velocityTable + layer.velocityCurve * 10;
    if (!Has(m_data, curve, 10)) {
        error = "tone velocity curve is truncated";
        return std::nullopt;
    }
    std::uint8_t startVelocity = 0;
    std::uint8_t startLevel = 0;
    std::uint8_t coefficient = m_data[curve];
    for (unsigned segment = 0; segment < 3; ++segment) {
        const std::size_t threshold = curve + 1 + segment * 3;
        if (velocity <= m_data[threshold])
            break;
        startVelocity = m_data[threshold];
        startLevel = m_data[threshold + 1];
        coefficient = m_data[threshold + 2];
    }

    std::uint16_t delta = static_cast<std::uint8_t>(velocity - startVelocity);
    const unsigned operation = coefficient & 7;
    const unsigned shift = coefficient >> 4;
    const bool fractional = (coefficient & 8) != 0;
    auto scaledLeft = [&]() -> std::uint16_t {
        if (!fractional)
            return static_cast<std::uint8_t>(delta << shift);
        return static_cast<std::uint16_t>((delta & 0x7F) * 12u << shift) >> 3;
    };
    switch (operation) {
    case 0:
    case 4:
        delta = startLevel;
        break;
    case 1:
        delta = static_cast<std::uint8_t>(scaledLeft() + startLevel);
        break;
    case 2:
        delta = static_cast<std::uint8_t>(delta + startLevel);
        break;
    case 3:
        delta = static_cast<std::uint8_t>((delta >> shift) + startLevel);
        break;
    case 5:
        delta = static_cast<std::uint8_t>(startLevel - (delta >> shift));
        break;
    case 6:
        delta = static_cast<std::uint8_t>(startLevel - delta);
        break;
    case 7:
        delta = static_cast<std::uint8_t>(startLevel - scaledLeft());
        break;
    }
    if ((delta & 0x80) != 0) {
        const std::uint8_t doubled = static_cast<std::uint8_t>(delta << 1);
        delta = (doubled & 0x80) != 0 ? 0 : 127;
    }
    delta &= 0x7F;
    // The carrier-volume arithmetic can be reexamined in the original code:
    //   v1.31: LEVEL1.MUS+0x308C (embedded driver+0x3068)
    //   v2.10-L: 0.BIN+0x3A44A (embedded driver+0x3A4E)
    if (driverVersion == SegaSoundDriverVersion::V1_31) {
        ++delta;
        const std::uint16_t layerLevel = 256u - layer.totalLevel;
        const std::uint16_t velocityProduct = delta * layerLevel;
        if (slotVelocity)
            *slotVelocity = velocityProduct;
        const std::uint32_t midiProduct =
            static_cast<std::uint32_t>(velocityProduct) *
            ((midiVolume & 0x7F) + 1u);
        const std::uint16_t amplitude =
            static_cast<std::uint16_t>((midiProduct * 4u - 1u) >> 16);
        const std::int8_t sequenceAdjustment =
            static_cast<std::int8_t>(sequenceVolume ^ 0x80);
        const int adjusted =
            std::clamp<int>(amplitude + sequenceAdjustment, 0, 255);
        return static_cast<std::uint8_t>(~adjusted);
    }
    if (delta == 0)
        delta = 1;

    int layerLevel = static_cast<std::uint8_t>(~layer.totalLevel) +
                     voice->volumeBias;
    layerLevel = std::clamp(layerLevel, 0, 255);
    std::uint16_t amplitude =
        static_cast<std::uint16_t>(delta * layerLevel) >> 8;
    if (slotVelocity)
        *slotVelocity = amplitude;
    amplitude = static_cast<std::uint16_t>(amplitude * midiVolume) >> 6;
    const std::int8_t sequenceAdjustment =
        static_cast<std::int8_t>(sequenceVolume ^ 0x80);
    const int adjusted = std::clamp<int>(amplitude + sequenceAdjustment, 0, 255);
    return static_cast<std::uint8_t>(~adjusted);
}

} // namespace saturn_sfx
