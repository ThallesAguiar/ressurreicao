#include <saturn_sndlib/sound_system.hpp>
#include <saturn_sndlib/tone_bank.hpp>

#include <ymir/hw/scsp/scsp_defs.hpp>
#include <ymir/savestate/savestate_scsp.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace saturn_sfx {

namespace {

constexpr std::size_t kBootClearSize = 0xB000;
constexpr std::size_t kMaximumAreaMapSize = 0x100;
constexpr std::size_t kCommandParameterOffset = 2;
constexpr std::size_t kCommandTimeoutSamples = 44100;

std::string Hex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << value;
    return out.str();
}

} // namespace

SoundSystem::SoundSystem(SegaSoundDriverVersion driverVersion)
    : m_scsp(m_scheduler, m_configuration.audio)
    , m_driverVersion(driverVersion) {
    m_scsp.MapMemory(m_bus);
    m_scsp.MapCallbacks({});
    m_scsp.SetSampleCallback(
        {this, [](sint16 left, sint16 right, void* context) {
             auto& self = *static_cast<SoundSystem*>(context);
             self.m_samples.push_back({static_cast<std::int16_t>(left),
                                       static_cast<std::int16_t>(right)});
         }});
}

bool SoundSystem::BootHle(
    std::span<const std::uint8_t> areaMap, std::string& error) {
    if (areaMap.empty() || areaMap.size() > kMaximumAreaMapSize) {
        error = "sound area map must contain 1.." +
                std::to_string(kMaximumAreaMapSize) + " bytes";
        return false;
    }
    m_scsp.SetCPUEnabled(false);
    m_scheduler.Reset();
    m_scsp.Reset(true);
    m_samples.clear();
    m_hle = true;
    m_hleAreaMap.assign(areaMap.begin(), areaMap.end());
    m_loadedPackages.clear();
    m_hleChannels = {};
    m_hleSlots = {};
    m_hleSequenceVolumes.fill(128);
    m_systemInfoAddress = 0;
    m_hostInterfaceAddress = 0;
    m_currentAreaMapAddress = 0;
    // MEM4MB=1, DAC18B=0, MVOL=15. This is the stable state after SND_Init
    // and the game's initial total-volume command.
    m_bus.Write<uint16>(kSCSPRegisterBase + 0x400, 0x020F);
    return true;
}

bool SoundSystem::Boot(
    std::span<const std::uint8_t> driver, std::span<const std::uint8_t> areaMap,
    std::string& error) {
    if (driver.size() < kSystemTableOffset + 12 ||
        driver.size() > kBootClearSize) {
        error = "sound driver size must be between " +
                std::to_string(kSystemTableOffset + 12) + " and " +
                std::to_string(kBootClearSize) + " bytes; got " +
                std::to_string(driver.size());
        return false;
    }
    if (areaMap.empty() || areaMap.size() > kMaximumAreaMapSize) {
        error = "area map must contain 1.." +
                std::to_string(kMaximumAreaMapSize) + " bytes; got " +
                std::to_string(areaMap.size());
        return false;
    }

    // SND_Init writes 0x0200 to SCSP register 0x400, clears 0xB000 bytes,
    // installs the driver at sound RAM zero, and then copies the area map to
    // the buffer described by the driver's system information table. SOTN's
    // matching SH-2 host routine is SND_Init at 0x060186C8; its source analogue
    // is SEGALIB/SND/SND_MAIN.C in the Sega Saturn toolkit. SND_ChgMap and the
    // sequence wrappers follow it at 0x06018848 and 0x060189F0.
    if (!BootImage(driver, error))
        return false;
    m_scsp.SetCPUEnabled(false);

    m_systemInfoAddress = Read32(kSystemTableOffset + 0x00);
    m_hostInterfaceAddress = Read32(kSystemTableOffset + 0x04);
    m_currentAreaMapAddress = Read32(kSystemTableOffset + 0x08);
    if (!IsSoundRange(m_systemInfoAddress, 0x40) ||
        !IsSoundRange(
            m_hostInterfaceAddress, kCommandBlockSize * kCommandBlockCount) ||
        !IsSoundRange(m_currentAreaMapAddress, 0x100)) {
        error =
            "driver system table contains an out-of-range pointer: sys=" +
            Hex(m_systemInfoAddress) + " host=" + Hex(m_hostInterfaceAddress) +
            " map=" + Hex(m_currentAreaMapAddress);
        return false;
    }

    const std::uint32_t areaMapBuffer = Read32(m_systemInfoAddress + 0x08);
    if (!IsSoundRange(
            areaMapBuffer, static_cast<std::uint32_t>(areaMap.size()))) {
        error = "driver area-map buffer is out of range: " + Hex(areaMapBuffer);
        return false;
    }
    WriteBlock(areaMapBuffer, areaMap);

    m_scsp.SetCPUEnabled(true);
    if (!ChangeMap(0, error) || !ChangeMap(0, error) ||
        !SetTotalVolume(127, error)) {
        return false;
    }
    m_samples.clear();
    return true;
}

bool SoundSystem::BootImage(
    std::span<const std::uint8_t> image, std::string& error) {
    if (image.size() < 8 || image.size() > kSoundRAMSize) {
        error = "sound RAM image must contain 8.." +
                std::to_string(kSoundRAMSize) + " bytes; got " +
                std::to_string(image.size());
        return false;
    }
    const auto read32 = [&](std::size_t offset) {
        return static_cast<std::uint32_t>(image[offset]) << 24 |
               static_cast<std::uint32_t>(image[offset + 1]) << 16 |
               static_cast<std::uint32_t>(image[offset + 2]) << 8 |
               image[offset + 3];
    };
    const std::uint32_t stack = read32(0);
    const std::uint32_t entry = read32(4);
    if (stack > kSoundRAMSize || entry >= kSoundRAMSize || (entry & 1) != 0) {
        error = "sound RAM image has invalid reset vectors: SP=" + Hex(stack) +
                " PC=" + Hex(entry);
        return false;
    }

    m_hle = false;
    m_hleAreaMap.clear();
    m_loadedPackages.clear();
    m_systemInfoAddress = 0;
    m_hostInterfaceAddress = 0;
    m_currentAreaMapAddress = 0;
    m_scsp.SetCPUEnabled(false);
    m_scheduler.Reset();
    m_scsp.Reset(true);
    m_samples.clear();
    m_bus.Write<uint16>(kSCSPRegisterBase + 0x400, 0x0200);
    WriteBlock(0, image);
    m_scsp.SetCPUEnabled(true);
    return true;
}

bool SoundSystem::ConfigureCommandInterface(
    std::uint32_t address, std::string& error) {
    if (!IsSoundRange(address, kCommandBlockSize * kCommandBlockCount) ||
        (address & 1) != 0) {
        error = "sound command interface is out of range: " + Hex(address);
        return false;
    }
    m_hostInterfaceAddress = address;
    return true;
}

bool SoundSystem::SendCommand(
    std::uint8_t command, std::span<const std::uint8_t> parameters,
    std::string& error) {
    if (m_hle) {
        error = "raw sound commands are not available with the HLE backend";
        return false;
    }
    return QueueCommand(command, parameters, error);
}

bool SoundSystem::CopySoundRam(
    std::uint32_t address, std::span<std::uint8_t> output,
    std::string& error) const {
    if (!IsSoundRange(address, static_cast<std::uint32_t>(output.size()))) {
        error = "sound RAM read is out of range: " + Hex(address);
        return false;
    }
    for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = Read8(address + static_cast<std::uint32_t>(i));
    return true;
}

bool SoundSystem::LoadSoundRam(
    std::uint32_t address, std::span<const std::uint8_t> data,
    std::string& error) {
    if (!IsSoundRange(address, static_cast<std::uint32_t>(data.size()))) {
        error = "sound RAM write is out of range: " + Hex(address);
        return false;
    }
    WriteBlock(address, data);
    return true;
}

bool SoundSystem::LoadDspProgram(
    std::span<const std::uint8_t> exb, std::uint32_t workAddress,
    std::uint32_t workSize, std::string& error) {
    constexpr std::size_t kExbBaseSize = 0x540;
    constexpr std::size_t kExbRegistersOffset = 0x40;
    constexpr std::uint32_t kDspRegistersOffset = 0x700;
    if (exb.size() < kExbBaseSize || workAddress >= kSoundRAMSize ||
        (workAddress & 0x1FFF) != 0 || workSize > kSoundRAMSize - workAddress) {
        error = "invalid Sega DSP program or work-RAM address";
        return false;
    }
    const std::uint8_t ringLength = exb[0x20];
    if (ringLength > 3) {
        error = "Sega DSP program has an invalid ring-buffer length";
        return false;
    }
    const std::uint32_t ringSize = 0x4000u << ringLength;
    if (ringSize + 0x40 > workSize) {
        error = "Sega DSP ring buffer extends beyond sound RAM";
        return false;
    }

    // The Sega driver initializes DSP work RAM with its 16-bit floating-point
    // zero representation (0x6000), not integer zero. The real-time control
    // block immediately after the ring buffer is ordinary integer storage.
    // This came from the v1.31 command-0x83 LLE trace, not an arbitrary fill;
    // integer-zero initialization produces severe clipping in Scorcher.
    for (std::uint32_t offset = 0; offset + 1 < workSize; offset += 2) {
        Write8(workAddress + offset, 0x60);
        Write8(workAddress + offset + 1, 0x00);
    }
    for (std::uint32_t offset = 0; offset < 0x40; ++offset)
        Write8(workAddress + ringSize + offset, 0);

    // EXB+0x40 contains the 0x80-byte COEF table, 0x80-byte MADRS table, and
    // 0x400-byte MPRO image in the exact SCSP register layout. EXB+0x20 is
    // RBL, +0x21 is the dynamic coefficient count, and +0x22 is the dynamic
    // filter count. Optional tables begin at EXB+0x540.
    for (std::size_t offset = kExbRegistersOffset;
         offset < kExbBaseSize; offset += 2) {
        const std::uint16_t value =
            static_cast<std::uint16_t>(exb[offset]) << 8 | exb[offset + 1];
        m_bus.Poke<uint16>(
            kSCSPRegisterBase + kDspRegistersOffset +
                static_cast<std::uint32_t>(offset - kExbRegistersOffset),
            value);
    }
    const std::uint16_t ringControl = static_cast<std::uint16_t>(
        ringLength << 7 | (workAddress >> 13 & 0x7F));
    m_bus.Poke<uint16>(kSCSPRegisterBase + 0x402, ringControl);
    return true;
}

bool SoundSystem::SetEffectMixer(
    std::span<const std::uint8_t> mixer, std::string& error) {
    if (mixer.size() != 16) {
        error = "Sega effect mixer must contain 16 output bytes";
        return false;
    }
    for (std::size_t output = 0; output < mixer.size(); ++output) {
        const auto address = kSCSPRegisterBase +
                             static_cast<std::uint32_t>(output * 0x20 + 0x16);
        const std::uint16_t current = m_bus.Peek<uint16>(address);
        m_bus.Poke<uint16>(
            address,
            static_cast<std::uint16_t>(current & 0xFF00 | mixer[output]));
    }
    return true;
}

M68kDebugState SoundSystem::DebugM68k() const {
    ymir::savestate::SCSPSaveState state{};
    m_scsp.SaveState(state);
    M68kDebugState result{};
    std::copy_n(state.m68k.DA.begin(), 8, result.data.begin());
    std::copy_n(state.m68k.DA.begin() + 8, 8, result.address.begin());
    result.pc = state.m68k.PC;
    result.status = state.m68k.SR;
    return result;
}

std::optional<AreaInfo> SoundSystem::FindArea(std::uint8_t id) const {
    // The active map at 0x500 consists of 8-byte records. The first byte is
    // the area ID, the next three bytes are its sound-RAM address, and the
    // second longword carries the load flag and maximum area size. 0xFF ends
    // the list. This mirrors SOTN func_06012C4C/GetSndMapInfo; Scorcher's
    // independent SH-2 resolver is SDDRVS.TSK 0x00202798. Both can be used to
    // recheck the record interpretation if another title differs.
    const std::size_t mapSize = m_hle ? m_hleAreaMap.size() : 0x100;
    for (std::uint32_t offset = 0; offset + 8 <= mapSize; offset += 8) {
        const std::uint32_t record = m_currentAreaMapAddress + offset;
        const auto byte = [&](std::uint32_t delta) {
            return m_hle ? m_hleAreaMap[offset + delta]
                         : Read8(record + delta);
        };
        const std::uint8_t recordID = byte(0);
        if (recordID == 0xFF) {
            break;
        }
        if (recordID == id) {
            const std::uint32_t address =
                (static_cast<std::uint32_t>(byte(1)) << 16) |
                (static_cast<std::uint32_t>(byte(2)) << 8) | byte(3);
            const std::uint32_t size =
                ((static_cast<std::uint32_t>(byte(4)) << 24) |
                 (static_cast<std::uint32_t>(byte(5)) << 16) |
                 (static_cast<std::uint32_t>(byte(6)) << 8) | byte(7)) &
                0x000F'FFFF;
            return AreaInfo{id, address, size};
        }
    }
    return std::nullopt;
}

bool SoundSystem::LoadArea(
    std::uint8_t id, std::span<const std::uint8_t> data, std::string& error) {
    const auto area = FindArea(id);
    if (!area) {
        error = "area " + Hex(id) + " is not present in the active sound map";
        return false;
    }
    // A CRT is a package, not one map record. It begins at the record selected
    // by its ID and may contain several consecutive sequence/tone/DSP areas.
    // For example SD_JO10.CRT starts at area 0x10 and spans the following
    // area-0 record. The individual record size is therefore diagnostic only;
    // the package is valid as long as the entire file remains in sound RAM.
    if (!IsSoundRange(area->address, static_cast<std::uint32_t>(data.size()))) {
        error = "area " + Hex(id) + " resolves outside sound RAM";
        return false;
    }
    WriteBlock(area->address, data);
    if (m_hle) {
        const auto existing = std::ranges::find_if(
            m_loadedPackages, [&](const LoadedPackage& package) {
                return package.address == area->address;
            });
        if (existing != m_loadedPackages.end())
            existing->data.assign(data.begin(), data.end());
        else
            m_loadedPackages.push_back(
                {area->address, {data.begin(), data.end()}});
    }
    return true;
}

bool SoundSystem::ChangeMap(std::uint8_t map, std::string& error) {
    if (m_hle) {
        if (map == 0)
            return true;
        error = "HLE currently supports sound map zero only";
        return false;
    }
    const std::array<std::uint8_t, 1> params{map};
    return QueueCommand(0x08, params, error);
}

bool SoundSystem::SetTotalVolume(std::uint8_t volume, std::string& error) {
    if (m_hle) {
        if (volume > 127) {
            error = "HLE total volume is out of range";
            return false;
        }
        const std::uint16_t control = m_bus.Peek<uint16>(
            kSCSPRegisterBase + 0x400);
        m_bus.Poke<uint16>(kSCSPRegisterBase + 0x400,
                           static_cast<std::uint16_t>(control & 0xFFF0 |
                                                      (volume >> 3)));
        return true;
    }
    const std::array<std::uint8_t, 1> params{volume};
    return QueueCommand(0x82, params, error);
}

bool SoundSystem::StartSequence(
    std::uint8_t slot, std::uint8_t bank, std::uint8_t song,
    std::uint8_t priority, std::string& error) {
    const std::array<std::uint8_t, 4> params{slot, bank, song, priority};
    return QueueCommand(0x01, params, error);
}

bool SoundSystem::SetSequenceVolume(std::uint8_t slot, std::uint8_t volume,
                                    std::uint8_t fade, std::string& error) {
    if (m_hle) {
        if (slot > 7 || fade != 0) {
            error = "invalid HLE sequence-volume request";
            return false;
        }
        m_hleSequenceVolumes[slot] =
            EncodeSequenceVolume(m_driverVersion, volume);
        for (std::uint8_t channel = 0; channel < 16; ++channel)
            UpdateHleVolumes(slot, channel);
        return true;
    }
    const std::array<std::uint8_t, 3> params{slot, volume, fade};
    return QueueCommand(0x05, params, error);
}

bool SoundSystem::StopSequence(std::uint8_t slot, std::string& error) {
    if (m_hle) {
        if (slot > 7) {
            error = "invalid HLE sequence slot";
            return false;
        }
        for (std::uint8_t channel = 0; channel < 16; ++channel)
            KeyOffHle(slot, channel, std::nullopt);
        return true;
    }
    const std::array<std::uint8_t, 1> params{slot};
    return QueueCommand(0x02, params, error);
}

bool SoundSystem::StopAll(std::string& error) {
    if (m_hle) {
        for (std::uint8_t slot = 0; slot < 8; ++slot)
            for (std::uint8_t channel = 0; channel < 16; ++channel)
                KeyOffHle(slot, channel, std::nullopt);
        return true;
    }
    return QueueCommand(0x0D, {}, error);
}

bool SoundSystem::SendMidi(
    std::uint8_t slot, std::uint8_t priority, std::uint8_t status,
    std::uint8_t data1, std::uint8_t data2, std::string& error) {
    const std::uint8_t kind = status >> 4;
    if (slot > 7 || kind < 8 || kind > 14) {
        error = "invalid direct MIDI request";
        return false;
    }
    const std::array<std::uint8_t, 4> params{
        static_cast<std::uint8_t>((priority & 0x1F) << 3 | (kind - 8)),
        static_cast<std::uint8_t>(slot << 5 | (status & 0x0F)), data1, data2};
    if (m_hle)
        return SendMidiHle(slot, status, data1, data2, error);
    return QueueCommand(0x09, params, error);
}

bool SoundSystem::SendMidiHle(
    std::uint8_t sequenceSlot, std::uint8_t status, std::uint8_t data1,
    std::uint8_t data2, std::string& error) {
    const std::uint8_t midiChannel = status & 0x0F;
    auto& channel = m_hleChannels[sequenceSlot][midiChannel];
    switch (status & 0xF0) {
    case 0x80:
        KeyOffHle(sequenceSlot, midiChannel, data1);
        return true;
    case 0x90:
        if (data2 == 0) {
            KeyOffHle(sequenceSlot, midiChannel, data1);
            return true;
        }
        break;
    case 0xB0:
        if (data1 == 0x20)
            channel.bank = data2;
        else if (data1 == 0x07) {
            channel.rawVolume = data2;
            // MIDI CC7/CC11 volume handling is at:
            //   v1.31: LEVEL1.MUS+0x28B0 (embedded driver+0x288C)
            //   v2.10-L: 0.BIN+0x39BBC (embedded driver+0x31C0)
            // v1.31 has one channel-volume field, so either controller
            // directly replaces it. v2.10-L keeps both and multiplies them.
            channel.volume =
                m_driverVersion == SegaSoundDriverVersion::V1_31
                    ? data2
                    : static_cast<std::uint8_t>(
                          channel.rawVolume * channel.expression >> 7);
            UpdateHleVolumes(sequenceSlot, midiChannel);
        } else if (data1 == 0x0B) {
            channel.expression = data2;
            channel.volume =
                m_driverVersion == SegaSoundDriverVersion::V1_31
                    ? data2
                    : static_cast<std::uint8_t>(
                          channel.rawVolume * channel.expression >> 7);
            UpdateHleVolumes(sequenceSlot, midiChannel);
        }
        else if (data1 == 0x0A) {
            const std::uint8_t linear = data2 >> 2;
            channel.pan = linear >= 16 ? linear - 16 : 31 - linear;
            channel.panOverride = true;
            for (std::size_t slot = 0; slot < m_hleSlots.size(); ++slot) {
                const auto& state = m_hleSlots[slot];
                if (!state.active || state.sequenceSlot != sequenceSlot ||
                    state.midiChannel != midiChannel)
                    continue;
                const auto address = kSCSPRegisterBase + slot * 0x20 + 0x16;
                const auto output = m_bus.Peek<uint16>(address);
                m_bus.Poke<uint16>(
                    address, static_cast<std::uint16_t>(
                                 (output & 0xE0FF) | (channel.pan << 8)));
            }
        }
        else if (data1 == 0x7B)
            KeyOffHle(sequenceSlot, midiChannel, std::nullopt);
        return true;
    case 0xC0:
        channel.program = data1;
        return true;
    case 0xE0: {
        const auto toneArea = FindArea(channel.bank);
        const LoadedPackage* package = nullptr;
        if (toneArea) {
            for (const auto& candidate : m_loadedPackages) {
                if (toneArea->address >= candidate.address &&
                    toneArea->address - candidate.address <
                        candidate.data.size()) {
                    package = &candidate;
                    break;
                }
            }
        }
        if (!toneArea || !package) {
            error = "HLE pitch bend has no loaded tone bank";
            return false;
        }
        auto tone = ToneBank::Open(
            std::span(package->data).subspan(
                toneArea->address - package->address),
            toneArea->address, error);
        const auto voice = tone ? tone->Voice(channel.program, error)
                                : std::nullopt;
        if (!voice)
            return false;
        const int bend = ((data2 & 0x7F) << 7 | (data1 & 0x7F)) - 0x2000;
        const unsigned rangeCode = voice->playModeAndBendRange & 0x0F;
        const unsigned semitones = rangeCode == 13 ? 24 : rangeCode;
        const int product = bend * static_cast<int>(semitones);
        channel.pitchTune = static_cast<std::int16_t>(
            product >= 0 ? product / 32 : -((-product + 31) / 32));
        for (std::size_t slot = 0; slot < m_hleSlots.size(); ++slot) {
            const auto& state = m_hleSlots[slot];
            if (!state.active || state.sequenceSlot != sequenceSlot ||
                state.midiChannel != midiChannel)
                continue;
            m_bus.Poke<uint16>(
                kSCSPRegisterBase + slot * 0x20 + 0x10,
                ComputeScspPitch(
                    m_driverVersion, state.baseNote, state.fineTune,
                    state.note, channel.pitchTune));
        }
        return true;
    }
    default:
        return true;
    }

    const auto toneArea = FindArea(channel.bank);
    if (!toneArea) {
        error = "HLE tone bank " + std::to_string(channel.bank) +
                " is not mapped";
        return false;
    }
    const LoadedPackage* package = nullptr;
    for (const auto& candidate : m_loadedPackages) {
        if (toneArea->address >= candidate.address &&
            toneArea->address - candidate.address < candidate.data.size()) {
            package = &candidate;
            break;
        }
    }
    if (!package) {
        error = "HLE tone bank " + std::to_string(channel.bank) +
                " has not been loaded";
        return false;
    }
    const std::size_t toneOffset = toneArea->address - package->address;
    auto tone = ToneBank::Open(
        std::span(package->data).subspan(toneOffset), toneArea->address, error);
    if (!tone)
        return false;
    const auto layers = tone->SelectLayers(channel.program, data1, error);
    if (layers.empty())
        return error.empty();

    bool keyed = false;
    for (const ToneLayer& layer : layers) {
        const auto freeSlot = std::ranges::find_if(
            m_hleSlots, [](const HleSlotState& slot) { return !slot.active; });
        if (freeSlot == m_hleSlots.end()) {
            error = "HLE has no free SCSP slots";
            return false;
        }
        const std::size_t slot = freeSlot - m_hleSlots.begin();
        *freeSlot = {true, sequenceSlot, midiChannel, data1, layer.baseNote,
                     layer.fineTune};
        auto image = BuildScspSlotImage(
            layer, toneArea->address, data1, channel.pitchTune,
            m_driverVersion);
        if (channel.panOverride) {
            image.registers[11] = static_cast<std::uint16_t>(
                (image.registers[11] & 0xE0FF) | (channel.pan << 8));
        }
        std::uint16_t slotVelocity = 0;
        const auto totalLevel = tone->TotalLevel(
            channel.program, layer, data2, channel.volume,
            m_hleSequenceVolumes[sequenceSlot], error, &slotVelocity,
            m_driverVersion);
        if (!totalLevel)
            return false;
        freeSlot->slotVelocity = slotVelocity;
        freeSlot->volumeControlled =
            !((layer.sourceAddress & (1u << 29)) == 0 &&
              (layer.effectInput & 7) == 0 &&
              (layer.directPan & 0xE0) == 0);
        image.registers[6] |= *totalLevel;
        // The low byte of register 0x16 is the global EFREG return mixer for
        // this slot number. Note-on updates only the direct-output high byte;
        // preserve the mixer value installed by Change Mixer (command 0x87).
        image.registers[11] |= m_bus.Peek<uint16>(
                                   kSCSPRegisterBase + slot * 0x20 + 0x16) &
                               0x00FF;
        for (std::size_t reg = 0; reg < image.registers.size(); ++reg)
            m_bus.Poke<uint16>(kSCSPRegisterBase + slot * 0x20 + reg * 2,
                               image.registers[reg]);

        const std::uint32_t work = 0x8000 + slot * 0x40;
        Write8(work + 0x02, midiChannel);
        Write8(work + 0x03, data1);
        Write32(work + 0x08,
                toneArea->address + static_cast<std::uint32_t>(layer.offset));
        Write16(work + 0x3A, 0x0008);
        keyed = true;
    }
    if (keyed)
        StrobeKeyExecute();
    return true;
}

void SoundSystem::KeyOffHle(
    std::uint8_t sequenceSlot, std::uint8_t midiChannel,
    std::optional<std::uint8_t> note) {
    bool changed = false;
    for (std::size_t slot = 0; slot < m_hleSlots.size(); ++slot) {
        auto& state = m_hleSlots[slot];
        if (!state.active || state.sequenceSlot != sequenceSlot ||
            state.midiChannel != midiChannel ||
            (note && state.note != *note))
            continue;
        const auto address = kSCSPRegisterBase + slot * 0x20;
        auto control = m_bus.Peek<uint16>(address);
        m_bus.Poke<uint16>(address,
                           static_cast<std::uint16_t>(control & ~0x0800));
        state.active = false;
        Write16(0x8000 + slot * 0x40 + 0x3A, 0);
        changed = true;
    }
    if (changed)
        StrobeKeyExecute();
}

void SoundSystem::StrobeKeyExecute() {
    const auto address = kSCSPRegisterBase;
    const auto control = m_bus.Peek<uint16>(address);
    m_bus.Poke<uint16>(address,
                       static_cast<std::uint16_t>(control | 0x1000));
}

void SoundSystem::UpdateHleVolumes(
    std::uint8_t sequenceSlot, std::uint8_t midiChannel) {
    const auto& channel = m_hleChannels[sequenceSlot][midiChannel];
    const std::int8_t sequenceAdjustment = static_cast<std::int8_t>(
        m_hleSequenceVolumes[sequenceSlot] ^ 0x80);
    for (std::size_t slot = 0; slot < m_hleSlots.size(); ++slot) {
        const auto& state = m_hleSlots[slot];
        if (!state.active || !state.volumeControlled ||
            state.sequenceSlot != sequenceSlot ||
            state.midiChannel != midiChannel)
            continue;
        std::uint16_t amplitude;
        if (m_driverVersion == SegaSoundDriverVersion::V1_31) {
            const std::uint32_t product =
                static_cast<std::uint32_t>(state.slotVelocity) *
                ((channel.volume & 0x7F) + 1u);
            amplitude =
                static_cast<std::uint16_t>((product * 4u - 1u) >> 16);
        } else {
            amplitude = static_cast<std::uint16_t>(
                state.slotVelocity * channel.volume) >> 6;
        }
        const int adjusted = std::clamp<int>(
            amplitude + sequenceAdjustment, 0, 255);
        const auto address = kSCSPRegisterBase + slot * 0x20 + 0x0C;
        const auto level = m_bus.Peek<uint16>(address);
        m_bus.Poke<uint16>(
            address, static_cast<std::uint16_t>(
                         (level & 0xFF00) |
                         static_cast<std::uint8_t>(~adjusted)));
    }
}

SequenceDebugState SoundSystem::DebugSequence(std::uint8_t slot) const {
    constexpr std::uint32_t kSequenceWork = 0x9900;
    constexpr std::uint32_t kSequenceWorkSize = 96;
    if (slot > 7)
        return {};
    const std::uint32_t work = kSequenceWork + slot * kSequenceWorkSize;
    return {
        (Read8(work + 0x00) & 0x80) != 0,
        Read16(work + 0x06),
        Read32(work + 0x08),
        Read8(work + 0x2D),
        Read8(work + 0x2E),
        Read8(work + 0x2F),
        Read32(work + 0x28) >> 8,
    };
}

std::array<SlotDebugState, 32> SoundSystem::DebugSlots() const {
    constexpr std::uint32_t kSlotWork = 0x8000;
    constexpr std::uint32_t kSlotWorkSize = 0x40;
    constexpr std::uint32_t kSlotRegisterSize = 0x20;
    std::array<SlotDebugState, 32> result{};
    for (std::uint8_t slot = 0; slot < result.size(); ++slot) {
        const std::uint32_t work = kSlotWork + slot * kSlotWorkSize;
        SlotDebugState& state = result[slot];
        state.slot = slot;
        state.active = (Read16(work + 0x3A) & 0x0008) != 0;
        state.midiChannel = Read8(work + 0x02);
        state.note = Read8(work + 0x03);
        state.layerAddress = Read32(work + 0x08);
        for (std::size_t reg = 0; reg < state.registers.size(); ++reg) {
            state.registers[reg] = m_bus.Peek<uint16>(
                kSCSPRegisterBase + slot * kSlotRegisterSize + reg * 2);
        }
    }
    return result;
}

void SoundSystem::BeginScspTrace() {
    m_traceScsp = true;
    m_traceSample = 0;
    m_scspTrace.clear();
    for (std::size_t slot = 0; slot < m_traceRegisters.size(); ++slot) {
        for (std::size_t reg = 0; reg < m_traceRegisters[slot].size(); ++reg) {
            m_traceRegisters[slot][reg] = m_bus.Peek<uint16>(
                kSCSPRegisterBase + slot * 0x20 + reg * 2);
        }
    }
}

void SoundSystem::ClearSamples() { m_samples.clear(); }

void SoundSystem::Render(std::size_t sampleCount) {
    m_samples.reserve(m_samples.size() + sampleCount);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        AdvanceOneSample();
    }
}

std::uint8_t SoundSystem::Read8(std::uint32_t soundAddress) const {
    return m_bus.Peek<uint8>(kSoundRAMBase + soundAddress);
}

std::uint16_t SoundSystem::Read16(std::uint32_t soundAddress) const {
    return m_bus.Peek<uint16>(kSoundRAMBase + soundAddress);
}

std::uint32_t SoundSystem::Read32(std::uint32_t soundAddress) const {
    return m_bus.Peek<uint32>(kSoundRAMBase + soundAddress);
}

void SoundSystem::Write8(std::uint32_t soundAddress, std::uint8_t value) {
    m_bus.Poke<uint8>(kSoundRAMBase + soundAddress, value);
}

void SoundSystem::Write16(std::uint32_t soundAddress, std::uint16_t value) {
    m_bus.Poke<uint16>(kSoundRAMBase + soundAddress, value);
}

void SoundSystem::Write32(std::uint32_t soundAddress, std::uint32_t value) {
    m_bus.Poke<uint32>(kSoundRAMBase + soundAddress, value);
}

void SoundSystem::WriteBlock(
    std::uint32_t soundAddress, std::span<const std::uint8_t> data) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        Write8(soundAddress + static_cast<std::uint32_t>(i), data[i]);
    }
}

bool SoundSystem::IsSoundRange(
    std::uint32_t address, std::uint32_t size) const {
    return address < kSoundRAMSize && size <= kSoundRAMSize - address;
}

bool SoundSystem::QueueCommand(
    std::uint8_t command, std::span<const std::uint8_t> parameters,
    std::string& error) {
    if (parameters.size() > kCommandBlockSize - kCommandParameterOffset) {
        error = "too many sound command parameters";
        return false;
    }

    std::uint32_t block = m_hostInterfaceAddress;
    bool found = false;
    for (std::size_t i = 0; i < kCommandBlockCount; ++i) {
        if (Read16(block) == 0) {
            found = true;
            break;
        }
        block += kCommandBlockSize;
    }
    if (!found) {
        error = "all sound command blocks are busy";
        return false;
    }

    for (std::size_t i = 0; i < parameters.size(); ++i) {
        Write8(block + kCommandParameterOffset + static_cast<std::uint32_t>(i),
               parameters[i]);
    }
    // The command word is written last, as it is the ownership handoff to the
    // 68000 driver. Sega commands occupy the high byte.
    Write16(block, static_cast<std::uint16_t>(command) << 8);
    return WaitForCommand(block, error);
}

bool SoundSystem::WaitForCommand(
    std::uint32_t commandAddress, std::string& error) {
    for (std::size_t i = 0; i < kCommandTimeoutSamples; ++i) {
        if (Read16(commandAddress) == 0) {
            return true;
        }
        AdvanceOneSample();
    }
    error = "sound command at " + Hex(commandAddress) +
            " was not consumed within one second";
    return false;
}

void SoundSystem::AdvanceOneSample() {
    m_scheduler.Advance(ymir::scsp::kCyclesPerSample);
    if (m_traceScsp)
        CaptureScspTrace();
}

void SoundSystem::CaptureScspTrace() {
    ++m_traceSample;
    for (std::size_t slot = 0; slot < m_traceRegisters.size(); ++slot) {
        std::array<std::uint16_t, 16> current = m_traceRegisters[slot];
        current[0] = m_bus.Peek<uint16>(
            kSCSPRegisterBase + slot * 0x20);
        const bool wasOn = (m_traceRegisters[slot][0] & 0x0800) != 0;
        const bool isOn = (current[0] & 0x0800) != 0;
        if (wasOn || isOn) {
            for (std::size_t reg = 1; reg < current.size(); ++reg) {
                current[reg] = m_bus.Peek<uint16>(
                    kSCSPRegisterBase + slot * 0x20 + reg * 2);
            }
        }
        // Ignore the driver's multi-instruction setup while KYONB is clear.
        // Capture the complete image at key-on, all live parameter changes,
        // and the final key-off transition.
        if ((isOn && current != m_traceRegisters[slot]) ||
            (wasOn && !isOn)) {
            m_scspTrace.push_back({m_traceSample, isOn, current});
        }
        m_traceRegisters[slot] = current;
    }
}

} // namespace saturn_sfx
