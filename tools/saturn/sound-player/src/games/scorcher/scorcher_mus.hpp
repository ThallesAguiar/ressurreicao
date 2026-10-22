#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace saturn_sfx::scorcher {

struct Payload {
    std::uint8_t id;
    std::uint32_t address;
    std::uint32_t readyFlagAddress;
    std::uint32_t readyFlagValue;
    std::vector<std::uint8_t> data;
};

struct MusImage {
    std::array<std::uint32_t, 9> header;
    std::vector<std::uint8_t> areaMap;
    std::vector<std::uint8_t> soundRam;
    std::vector<Payload> payloads;
};

std::optional<MusImage> ParseMus(
    std::span<const std::uint8_t> data, std::string& error);

} // namespace saturn_sfx::scorcher
