#include "scorcher_mus.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Write32(std::vector<std::uint8_t>& data, std::size_t offset,
             std::uint32_t value) {
    data[offset + 0] = static_cast<std::uint8_t>(value >> 24);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    data[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    data[offset + 3] = static_cast<std::uint8_t>(value);
}

void WriteArea(std::vector<std::uint8_t>& data, std::size_t offset,
               std::uint8_t id, std::uint32_t address, std::uint32_t size) {
    data[offset] = id;
    data[offset + 1] = static_cast<std::uint8_t>(address >> 16);
    data[offset + 2] = static_cast<std::uint8_t>(address >> 8);
    data[offset + 3] = static_cast<std::uint8_t>(address);
    Write32(data, offset + 4, size);
}

bool Check(bool condition, std::string_view message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

} // namespace

int main() {
    constexpr std::size_t kMap = 0x5D94;
    constexpr std::size_t kTone = 0x5DAC;
    constexpr std::size_t kSequence = 0x5DB0;
    constexpr std::size_t kEffect = 0x5DB4;
    std::vector<std::uint8_t> file(kEffect + 2);

    const std::uint32_t header[] = {
        9, kMap / 4, 24, kTone / 4, 4,
        kSequence / 4, 3, kEffect / 4, 2,
    };
    for (std::size_t i = 0; i < std::size(header); ++i)
        Write32(file, i * 4, header[i]);
    Write32(file, 0x24, 0xA000);
    Write32(file, 0x28, 0x1000);
    constexpr std::string_view version = "Ver1.31 95/06/20";
    std::ranges::copy(version, file.begin() + 0x1044);
    std::ranges::copy(std::string_view{"SEGA"}, file.begin() + kMap - 4);
    WriteArea(file, kMap + 0, 0x00, 0x10000, 4);
    WriteArea(file, kMap + 8, 0x10, 0x11000, 3);
    WriteArea(file, kMap + 16, 0x20, 0x12000, 2);
    std::ranges::copy(std::initializer_list<std::uint8_t>{1, 2, 3, 4},
                      file.begin() + kTone);
    std::ranges::copy(std::initializer_list<std::uint8_t>{5, 6, 7},
                      file.begin() + kSequence);
    std::ranges::copy(std::initializer_list<std::uint8_t>{8, 9},
                      file.begin() + kEffect);

    std::string error;
    const auto mus = saturn_sfx::scorcher::ParseMus(file, error);
    bool ok = Check(mus.has_value(), error);
    if (!mus) return 1;
    ok &= Check(mus->soundRam.size() == 512 * 1024,
                "boot image must fill Saturn sound RAM");
    ok &= Check(mus->soundRam[0] == 0 && mus->soundRam[1] == 0 &&
                    mus->soundRam[2] == 0xA0,
                "driver reset vector was not copied to address zero");
    ok &= Check(mus->soundRam[0xA000] == 0x00 &&
                    mus->soundRam[0xA008] == 0x10 &&
                    mus->soundRam[0xA010] == 0x20,
                "area map was not copied from header[1]");
    ok &= Check(mus->soundRam[0x10000] == 0,
                "payloads must not be loaded before ChangeMap");
    ok &= Check(mus->payloads.size() == 3,
                "expected all three post-map payloads");
    if (mus->payloads.size() == 3) {
        ok &= Check(mus->payloads[0].id == 0x00 &&
                        mus->payloads[0].address == 0x10000 &&
                        mus->payloads[0].readyFlagAddress == 0x504 &&
                        mus->payloads[0].readyFlagValue == 0x80000004 &&
                        mus->payloads[0].data ==
                            std::vector<std::uint8_t>({1, 2, 3, 4}),
                    "tone payload metadata mismatch");
        ok &= Check(mus->payloads[1].id == 0x10 &&
                        mus->payloads[1].address == 0x11000 &&
                        mus->payloads[1].readyFlagAddress == 0x50C &&
                        mus->payloads[1].readyFlagValue == 0x80000003 &&
                        mus->payloads[1].data ==
                            std::vector<std::uint8_t>({5, 6, 7}),
                    "sequence payload metadata mismatch");
        ok &= Check(mus->payloads[2].id == 0x20 &&
                        mus->payloads[2].address == 0x12000 &&
                        mus->payloads[2].readyFlagAddress == 0x514 &&
                        mus->payloads[2].readyFlagValue == 0x80000002 &&
                        mus->payloads[2].data ==
                            std::vector<std::uint8_t>({8, 9}),
                    "effect payload metadata mismatch");
    }

    auto truncated = file;
    truncated.pop_back();
    error.clear();
    ok &= Check(!saturn_sfx::scorcher::ParseMus(truncated, error),
                "truncated payload should be rejected");
    return ok ? 0 : 1;
}
