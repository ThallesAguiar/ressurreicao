#include "stage_sfx.hpp"
#include "sotn_data.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <optional>
#include <unordered_set>

namespace saturn_sfx::sotn {

namespace {

// GAME.PRG is loaded at 0x06004080. Its stage-file table has 100 24-byte
// records; record+0x0C is a 0xB00-based index into g_SaturnSoundFiles. These
// values can be rechecked from the table consumers in GAME.PRG and from the
// named symbols in the Saturn decomp configuration.
constexpr std::size_t kStageFileTableOffset = 0x43798;
constexpr std::size_t kStageFileCount = 100;
constexpr std::size_t kStageFileRecordSize = 24;
constexpr std::uint32_t kGameLoadAddress = 0x06004080;
constexpr std::uint32_t kPlaySfx = 0x06011278;
constexpr std::uint32_t kPlaySfxVolPan = 0x06011AA0;
constexpr std::uint32_t kPlaySfxPositional = 0x0607B714;

std::uint16_t ReadBE16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size())
        return 0;
    return static_cast<std::uint16_t>(data[offset] << 8 | data[offset + 1]);
}

std::uint32_t ReadBE32(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size())
        return 0;
    return static_cast<std::uint32_t>(data[offset]) << 24 |
           static_cast<std::uint32_t>(data[offset + 1]) << 16 |
           static_cast<std::uint32_t>(data[offset + 2]) << 8 | data[offset + 3];
}

std::optional<std::string> ReadGameString(
    std::span<const std::uint8_t> game, std::uint32_t address) {
    if (address < kGameLoadAddress)
        return std::nullopt;
    const std::size_t offset = address - kGameLoadAddress;
    if (offset >= game.size())
        return std::nullopt;
    std::string result;
    for (std::size_t i = offset; i < game.size() && game[i] != 0; ++i) {
        if (result.size() == 31)
            return std::nullopt;
        result.push_back(static_cast<char>(game[i]));
    }
    return result.empty() ? std::nullopt
                          : std::optional<std::string>{std::move(result)};
}

std::optional<std::vector<std::uint8_t>> ReadFile(
    const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path.string();
        return std::nullopt;
    }
    std::vector<std::uint8_t> data{
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        error = "failed while reading " + path.string();
        return std::nullopt;
    }
    return data;
}

bool IsSfxFunction(std::uint32_t address) {
    return address == kPlaySfx || address == kPlaySfxVolPan ||
           address == kPlaySfxPositional;
}

std::size_t PCLiteralOffset(
    std::size_t instruction, std::uint8_t displacement, std::size_t scale) {
    const std::size_t pc =
        scale == 4 ? (instruction + 4) & ~std::size_t{3} : instruction + 4;
    return pc + displacement * scale;
}

struct FlowState {
    std::size_t pc;
    std::uint16_t targetRegisters;
    std::int32_t r4; // -1 means unknown

    bool operator==(const FlowState&) const = default;
};

struct FlowStateHash {
    std::size_t operator()(const FlowState& state) const {
        std::size_t hash = state.pc;
        hash ^= static_cast<std::size_t>(state.targetRegisters) << 24;
        hash ^= static_cast<std::size_t>(state.r4 + 1) << 40;
        return hash;
    }
};

std::optional<std::size_t> BranchTarget(
    std::size_t pc, std::uint16_t instruction) {
    std::int32_t displacement;
    if ((instruction & 0xF000) == 0xA000 || (instruction & 0xF000) == 0xB000) {
        displacement = instruction & 0x0FFF;
        if (displacement & 0x800)
            displacement -= 0x1000;
    } else if (
        (instruction & 0xFF00) == 0x8900 || (instruction & 0xFF00) == 0x8B00 ||
        (instruction & 0xFF00) == 0x8D00 || (instruction & 0xFF00) == 0x8F00) {
        displacement = static_cast<std::int8_t>(instruction);
    } else {
        return std::nullopt;
    }
    const std::int64_t target = static_cast<std::int64_t>(pc) + 4 +
                                static_cast<std::int64_t>(displacement) * 2;
    if (target < 0)
        return std::nullopt;
    return static_cast<std::size_t>(target);
}

bool IsControlTransfer(std::uint16_t instruction) {
    const bool directBranch =
        (instruction & 0xF000) == 0xA000 || (instruction & 0xF000) == 0xB000 ||
        (instruction & 0xFF00) == 0x8900 || (instruction & 0xFF00) == 0x8B00 ||
        (instruction & 0xFF00) == 0x8D00 || (instruction & 0xFF00) == 0x8F00;
    return directBranch || instruction == 0x000B || instruction == 0x002B ||
           (instruction & 0xF0FF) == 0x400B ||
           (instruction & 0xF0FF) == 0x402B ||
           (instruction & 0xF0FF) == 0x0003 || (instruction & 0xF0FF) == 0x0023;
}

bool WritesRegister(std::uint16_t instruction, unsigned reg) {
    const unsigned high = instruction >> 12;
    const unsigned destination = instruction >> 8 & 0xF;
    if (destination != reg)
        return false;
    if (high == 5 || high == 6 || high == 7 || high == 9 || high == 0xD ||
        high == 0xE) {
        return true;
    }
    if (high == 2) {
        const unsigned operation = instruction & 0xF;
        return operation == 4 || operation == 5 || operation == 6 ||
               operation == 9 || operation == 0xA || operation == 0xB ||
               operation == 0xD;
    }
    if (high == 3) {
        const unsigned operation = instruction & 0xF;
        return operation == 4 || operation == 8 || operation == 0xA ||
               operation == 0xB || operation == 0xC || operation == 0xE ||
               operation == 0xF;
    }
    if (high == 4) {
        const unsigned operation = instruction & 0x3F;
        return operation == 0x00 || operation == 0x01 || operation == 0x04 ||
               operation == 0x05 || operation == 0x08 || operation == 0x09 ||
               operation == 0x10 || operation == 0x18 || operation == 0x19 ||
               operation == 0x20 || operation == 0x21 || operation == 0x24 ||
               operation == 0x25 || operation == 0x28 || operation == 0x29;
    }
    return false;
}

void SetTargetRegister(FlowState& state, unsigned reg, bool target) {
    const std::uint16_t bit = static_cast<std::uint16_t>(1U << reg);
    if (target)
        state.targetRegisters |= bit;
    else
        state.targetRegisters &= ~bit;
}

void ApplyInstruction(std::span<const std::uint8_t> data, FlowState& state) {
    const std::uint16_t instruction = ReadBE16(data, state.pc);
    const unsigned destination = instruction >> 8 & 0xF;

    if ((instruction & 0xF00F) == 0x6003) { // mov Rm,Rn
        const unsigned source = instruction >> 4 & 0xF;
        SetTargetRegister(
            state, destination, state.targetRegisters & (1U << source));
        if (destination == 4 && source != 4)
            state.r4 = -1;
        return;
    }

    if ((instruction & 0xF000) == 0xE000) { // mov #imm,Rn
        SetTargetRegister(state, destination, false);
        if (destination == 4)
            state.r4 = static_cast<std::int8_t>(instruction);
        return;
    }
    if ((instruction & 0xF000) == 0x9000) { // mov.w @(disp,pc),Rn
        const std::size_t literal =
            PCLiteralOffset(state.pc, instruction & 0xFF, 2);
        SetTargetRegister(state, destination, false);
        if (destination == 4)
            state.r4 = ReadBE16(data, literal);
        return;
    }
    if ((instruction & 0xF000) == 0xD000) { // mov.l @(disp,pc),Rn
        const std::size_t literal =
            PCLiteralOffset(state.pc, instruction & 0xFF, 4);
        const std::uint32_t value = ReadBE32(data, literal);
        SetTargetRegister(state, destination, IsSfxFunction(value));
        if (destination == 4)
            state.r4 = value <= 0xFFFF ? static_cast<std::int32_t>(value) : -1;
        return;
    }

    if (WritesRegister(instruction, destination)) {
        SetTargetRegister(state, destination, false);
        if (destination == 4)
            state.r4 = -1;
    }
}

std::int32_t FindR4AtBlockPoint(
    std::span<const std::uint8_t> data, std::size_t point) {
    // Recover only definitions on the straight-line path immediately before
    // the pointer load. Crossing a control transfer would require predecessor
    // analysis and risks treating a value from another path as the argument.
    for (std::size_t cursor = point; cursor >= 2;) {
        cursor -= 2;
        const std::uint16_t instruction = ReadBE16(data, cursor);
        if (IsControlTransfer(instruction))
            break;
        if ((instruction & 0xFF00) == 0xE400)
            return static_cast<std::int8_t>(instruction);
        if ((instruction & 0xFF00) == 0x9400) {
            return ReadBE16(
                data, PCLiteralOffset(cursor, instruction & 0xFF, 2));
        }
        if ((instruction & 0xFF00) == 0xD400) {
            const std::uint32_t value =
                ReadBE32(data, PCLiteralOffset(cursor, instruction & 0xFF, 4));
            return value <= 0xFFFF ? static_cast<std::int32_t>(value) : -1;
        }
        if (WritesRegister(instruction, 4))
            return -1;
    }
    return -1;
}

void InvalidateCallerSaved(FlowState& state) {
    state.targetRegisters &= 0xFF00;
    state.r4 = -1;
}

void FollowSfxPointer(std::span<const std::uint8_t> data, FlowState initial,
                      std::vector<std::uint16_t>& result) {
    std::deque<FlowState> pending{initial};
    std::unordered_set<FlowState, FlowStateHash> visited;

    auto enqueue = [&](FlowState state, std::size_t pc) {
        if (pc + 2 <= data.size() && state.targetRegisters != 0) {
            state.pc = pc;
            pending.push_back(state);
        }
    };

    while (!pending.empty()) {
        FlowState state = pending.front();
        pending.pop_front();
        if (!visited.insert(state).second)
            continue;

        const std::uint16_t instruction = ReadBE16(data, state.pc);
        if ((instruction & 0xF0FF) == 0x400B) { // jsr @Rn
            const unsigned reg = instruction >> 8 & 0xF;
            const bool targetCall = state.targetRegisters & (1U << reg);
            FlowState delayed = state;
            delayed.pc += 2;
            ApplyInstruction(data, delayed);
            if (targetCall) {
                if (delayed.r4 >= kFirstSfxID && delayed.r4 <= kLastSfxID)
                    result.push_back(static_cast<std::uint16_t>(delayed.r4));
                continue;
            }
            InvalidateCallerSaved(delayed);
            enqueue(delayed, state.pc + 4);
            continue;
        }
        if (instruction == 0x000B || instruction == 0x002B ||
            (instruction & 0xF0FF) == 0x402B ||
            (instruction & 0xF0FF) == 0x0023) {
            continue; // return, exception return, or tail call
        }
        if ((instruction & 0xF0FF) == 0x0003) { // bsrf Rn
            FlowState delayed = state;
            delayed.pc += 2;
            ApplyInstruction(data, delayed);
            InvalidateCallerSaved(delayed);
            enqueue(delayed, state.pc + 4);
            continue;
        }

        if (const auto target = BranchTarget(state.pc, instruction)) {
            const bool delayedBranch =
                (instruction & 0xF000) == 0xA000 ||
                (instruction & 0xF000) == 0xB000 ||
                (instruction & 0xFF00) == 0x8D00 ||
                (instruction & 0xFF00) == 0x8F00;
            FlowState branched = state;
            if (delayedBranch) {
                branched.pc += 2;
                ApplyInstruction(data, branched);
            }
            if ((instruction & 0xF000) == 0xB000) { // bsr
                InvalidateCallerSaved(branched);
                enqueue(branched, state.pc + 4);
            } else {
                enqueue(branched, *target);
                if ((instruction & 0xFF00) == 0x8900 ||
                    (instruction & 0xFF00) == 0x8B00) {
                    enqueue(state, state.pc + 2);
                } else if ((instruction & 0xFF00) == 0x8D00 ||
                           (instruction & 0xFF00) == 0x8F00) {
                    enqueue(branched, state.pc + 4);
                }
            }
            continue;
        }

        ApplyInstruction(data, state);
        enqueue(state, state.pc + 2);
    }
}

} // namespace

std::vector<StageSoundSet> FindStageSoundSets(
    std::span<const std::uint8_t> game,
    const std::filesystem::path& gameDirectory) {
    const auto soundFiles = ReadSoundFiles(game);
    if (soundFiles.empty())
        return {};

    std::vector<StageSoundSet> result;
    std::unordered_set<std::string> seenOverlays;
    for (std::size_t i = 0; i < kStageFileCount; ++i) {
        const std::size_t record =
            kStageFileTableOffset + i * kStageFileRecordSize;
        if (record + kStageFileRecordSize > game.size())
            break;
        const auto overlay = ReadGameString(game, ReadBE32(game, record));
        const std::uint32_t soundCommand = ReadBE32(game, record + 0x0C);
        if (!overlay || soundCommand < 0xB00 ||
            soundCommand >= 0xB00 + soundFiles.size()) {
            continue;
        }
        const std::string& soundPackage =
            soundFiles[soundCommand - 0xB00].name;
        if (seenOverlays.contains(*overlay) ||
            !std::filesystem::exists(gameDirectory / *overlay) ||
            !std::filesystem::exists(gameDirectory / "SD" / soundPackage)) {
            continue;
        }
        seenOverlays.insert(*overlay);
        result.push_back({*overlay, soundPackage});
    }
    return result;
}

std::vector<std::uint16_t> ScanOverlaySfx(
    const std::filesystem::path& overlay, std::string& error) {
    const auto file = ReadFile(overlay, error);
    if (!file)
        return {};
    const std::span<const std::uint8_t> data = *file;
    std::vector<std::uint16_t> result;

    // Saturn GCC normally materializes an absolute callee through a PC-relative
    // literal and calls it with jsr @Rn. Do not impose a fixed literal-pool
    // distance: PCLiteralOffset implements the SH-2 instruction itself, then
    // FollowSfxPointer explores control flow until the value is killed or
    // called. Constant R4 values are reported because R4 is the first SH-2 ABI
    // argument. The target addresses are PlaySfx, PlaySfxVolPan, and
    // PlaySfxPositional at 0x06011278, 0x06011AA0, and 0x0607B714. If a new
    // compiler call form is missed, add a fixture to stage_sfx_test.cpp before
    // broadening the dataflow rules.
    for (std::size_t pc = 0; pc + 4 <= data.size(); pc += 2) {
        const std::uint16_t load = ReadBE16(data, pc);
        if ((load & 0xF000) != 0xD000) // mov.l @(disp,pc),Rn
            continue;
        const std::size_t literal = PCLiteralOffset(pc, load & 0xFF, 4);
        if (!IsSfxFunction(ReadBE32(data, literal)))
            continue;
        FlowState state{pc, 0, FindR4AtBlockPoint(data, pc)};
        ApplyInstruction(data, state);
        state.pc += 2;
        FollowSfxPointer(data, state, result);
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace saturn_sfx::sotn
