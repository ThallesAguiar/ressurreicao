#include <saturn_sndlib/package.hpp>

#include "sotn_data.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

} // namespace

int main() {
    const std::filesystem::path root = SOTN_GAME_DIR;
    const auto game = Read(root / "0.BIN");
    const auto original = Read(root / "SD/SD_ROU.CRT");
    std::string error;
    const auto boot = saturn_sfx::sotn::ExtractBootAssets(game, error);
    if (!boot || original.empty()) {
        std::cerr << "cannot load SOTN package fixture: " << error << '\n';
        return 1;
    }
    const auto unpacked = saturn_sfx::UnpackSoundPackage(
        original, boot->areaMap, 0x12, error);
    if (!unpacked) {
        std::cerr << "cannot unpack SD_ROU.CRT: " << error << '\n';
        return 1;
    }
    const auto repacked = saturn_sfx::PackSoundPackage(*unpacked, error);
    if (!repacked) {
        std::cerr << "cannot repack SD_ROU.CRT: " << error << '\n';
        return 1;
    }
    if (*repacked != original) {
        std::cerr << "SD_ROU.CRT did not round-trip byte-exactly\n";
        return 1;
    }
    if (unpacked->areas.size() != 3 ||
        unpacked->areas[0].map.id != 0x12 ||
        unpacked->areas[1].map.id != 0x13 ||
        unpacked->areas[2].map.id != 0x02) {
        std::cerr << "SD_ROU.CRT area layout was not decoded as expected\n";
        return 1;
    }
    std::cout << "SD_ROU.CRT round-tripped " << original.size()
              << " bytes across " << unpacked->areas.size() << " areas\n";
    return 0;
}
