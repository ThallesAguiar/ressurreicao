#pragma once

// Standalone Saturn SNDLIB emulation API.

#include <saturn_sndlib/driver_version.hpp>

#include <ymir/core/configuration.hpp>
#include <ymir/core/scheduler.hpp>
#include <ymir/hw/scsp/scsp.hpp>
#include <ymir/sys/bus.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace saturn_sfx {

struct StereoSample {
    std::int16_t left;
    std::int16_t right;
};

struct AreaInfo {
    std::uint8_t id;
    std::uint32_t address;
    std::uint32_t size;
};

struct SequenceDebugState {
    bool active;
    std::uint16_t resolution;
    std::uint32_t tempo;
    std::uint8_t status;
    std::uint8_t data1;
    std::uint8_t data2;
    std::uint32_t gate;
};

struct SlotDebugState {
    std::uint8_t slot;
    bool active;
    std::uint8_t midiChannel;
    std::uint8_t note;
    std::uint32_t layerAddress;
    std::array<std::uint16_t, 16> registers;
};

struct ScspTraceEvent {
    std::uint64_t sample;
    bool keyOn;
    std::array<std::uint16_t, 16> registers;
};

struct M68kDebugState {
    std::array<std::uint32_t, 8> data;
    std::array<std::uint32_t, 8> address;
    std::uint32_t pc;
    std::uint16_t status;
};

class SoundSystem {
  public:
    explicit SoundSystem(
        SegaSoundDriverVersion driverVersion =
            SegaSoundDriverVersion::V2_10L);

    bool Boot(std::span<const std::uint8_t> driver,
              std::span<const std::uint8_t> areaMap, std::string& error);
    bool BootImage(std::span<const std::uint8_t> image, std::string& error);
    bool BootHle(std::span<const std::uint8_t> areaMap, std::string& error);
    bool ConfigureCommandInterface(std::uint32_t address,
                                   std::string& error);
    bool SendCommand(std::uint8_t command,
                     std::span<const std::uint8_t> parameters,
                     std::string& error);
    bool CopySoundRam(std::uint32_t address, std::span<std::uint8_t> output,
                      std::string& error) const;
    bool LoadSoundRam(std::uint32_t address,
                      std::span<const std::uint8_t> data,
                      std::string& error);
    bool LoadDspProgram(std::span<const std::uint8_t> exb,
                        std::uint32_t workAddress, std::uint32_t workSize,
                        std::string& error);
    bool SetEffectMixer(std::span<const std::uint8_t> mixer,
                        std::string& error);

    std::optional<AreaInfo> FindArea(std::uint8_t id) const;
    bool LoadArea(std::uint8_t id, std::span<const std::uint8_t> data,
                  std::string& error);

    bool ChangeMap(std::uint8_t map, std::string& error);
    bool SetTotalVolume(std::uint8_t volume, std::string& error);
    bool StartSequence(std::uint8_t slot, std::uint8_t bank, std::uint8_t song,
                       std::uint8_t priority, std::string& error);
    bool SetSequenceVolume(std::uint8_t slot, std::uint8_t volume,
                           std::uint8_t fade, std::string& error);
    bool StopSequence(std::uint8_t slot, std::string& error);
    bool StopAll(std::string& error);
    bool SendMidi(std::uint8_t slot, std::uint8_t priority,
                  std::uint8_t status, std::uint8_t data1,
                  std::uint8_t data2, std::string& error);

    SequenceDebugState DebugSequence(std::uint8_t slot) const;
    std::array<SlotDebugState, 32> DebugSlots() const;
    M68kDebugState DebugM68k() const;
    void BeginScspTrace();
    const std::vector<ScspTraceEvent>& ScspTrace() const {
        return m_scspTrace;
    }

    void ClearSamples();
    void Render(std::size_t sampleCount);
    const std::vector<StereoSample>& Samples() const { return m_samples; }
    void TrimSamples(std::size_t size) { m_samples.resize(size); }

    std::uint32_t SystemInfoAddress() const { return m_systemInfoAddress; }
    std::uint32_t HostInterfaceAddress() const {
        return m_hostInterfaceAddress;
    }
    std::uint32_t CurrentAreaMapAddress() const {
        return m_currentAreaMapAddress;
    }

  private:
    struct LoadedPackage {
        std::uint32_t address;
        std::vector<std::uint8_t> data;
    };
    struct HleChannelState {
        std::uint8_t bank = 0;
        std::uint8_t program = 0;
        std::uint8_t rawVolume = 127;
        std::uint8_t expression = 127;
        std::uint8_t volume = 127;
        std::uint8_t pan = 0;
        bool panOverride = false;
        std::int16_t pitchTune = 0;
    };
    struct HleSlotState {
        bool active = false;
        std::uint8_t sequenceSlot = 0;
        std::uint8_t midiChannel = 0;
        std::uint8_t note = 0;
        std::uint8_t baseNote = 0;
        std::int8_t fineTune = 0;
        std::uint16_t slotVelocity = 0;
        bool volumeControlled = false;
    };

    static constexpr std::uint32_t kSoundRAMBase = 0x05A0'0000;
    static constexpr std::uint32_t kSCSPRegisterBase = 0x05B0'0000;
    static constexpr std::uint32_t kSystemTableOffset = 0x400;
    static constexpr std::uint32_t kSoundRAMSize = 512 * 1024;
    static constexpr std::size_t kCommandBlockSize = 0x10;
    static constexpr std::size_t kCommandBlockCount = 8;

    std::uint8_t Read8(std::uint32_t soundAddress) const;
    std::uint16_t Read16(std::uint32_t soundAddress) const;
    std::uint32_t Read32(std::uint32_t soundAddress) const;
    void Write8(std::uint32_t soundAddress, std::uint8_t value);
    void Write16(std::uint32_t soundAddress, std::uint16_t value);
    void Write32(std::uint32_t soundAddress, std::uint32_t value);
    void WriteBlock(
        std::uint32_t soundAddress, std::span<const std::uint8_t> data);

    bool IsSoundRange(std::uint32_t address, std::uint32_t size = 1) const;
    bool QueueCommand(
        std::uint8_t command, std::span<const std::uint8_t> parameters,
        std::string& error);
    bool WaitForCommand(std::uint32_t commandAddress, std::string& error);
    bool SendMidiHle(std::uint8_t slot, std::uint8_t status,
                     std::uint8_t data1, std::uint8_t data2,
                     std::string& error);
    void KeyOffHle(std::uint8_t sequenceSlot, std::uint8_t midiChannel,
                   std::optional<std::uint8_t> note);
    void StrobeKeyExecute();
    void UpdateHleVolumes(std::uint8_t sequenceSlot,
                          std::uint8_t midiChannel);
    void AdvanceOneSample();
    void CaptureScspTrace();

    ymir::core::Scheduler m_scheduler;
    ymir::core::Configuration m_configuration;
    ymir::sys::SH2Bus m_bus;
    ymir::scsp::SCSP m_scsp;
    std::vector<StereoSample> m_samples;

    std::uint32_t m_systemInfoAddress = 0;
    std::uint32_t m_hostInterfaceAddress = 0;
    std::uint32_t m_currentAreaMapAddress = 0;
    bool m_hle = false;
    std::vector<std::uint8_t> m_hleAreaMap;
    std::vector<LoadedPackage> m_loadedPackages;
    std::array<std::array<HleChannelState, 16>, 8> m_hleChannels{};
    std::array<HleSlotState, 32> m_hleSlots{};
    std::array<std::uint8_t, 8> m_hleSequenceVolumes{};
    bool m_traceScsp = false;
    std::uint64_t m_traceSample = 0;
    std::array<std::array<std::uint16_t, 16>, 32> m_traceRegisters{};
    std::vector<ScspTraceEvent> m_scspTrace;
    SegaSoundDriverVersion m_driverVersion;
};

} // namespace saturn_sfx
