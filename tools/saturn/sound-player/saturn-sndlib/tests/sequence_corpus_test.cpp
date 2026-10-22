#include <saturn_sndlib/sequence.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

namespace {

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

} // namespace

int main(int argc, char**) {
    const bool report = argc > 1;
    const std::filesystem::path directory =
        std::filesystem::path(SATURN_SNDLIB_SOTN_GAME_DIR) /
        "SD";
    std::map<unsigned, std::size_t> statusCounts;
    std::map<unsigned, std::size_t> controllerCounts;
    std::array<std::size_t, 2> portCounts{};
    std::size_t decodedSongs = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".CRT")
            continue;
        const auto data = Read(entry.path());
        if (data.size() < 2)
            continue;
        const unsigned songs = static_cast<unsigned>(data[0]) << 8 | data[1];
        if (songs == 0 || songs > 256)
            continue;
        for (unsigned song = 0; song < songs; ++song) {
            std::string error;
            auto decoder = saturn_sfx::SequenceDecoder::Open(
                data, static_cast<std::uint8_t>(song), error);
            if (!decoder) {
                std::cerr << entry.path().filename().string() << " song "
                          << song << ": " << error << '\n';
                return 1;
            }
            ++decodedSongs;
            for (unsigned count = 0; count < 4096; ++count) {
                const auto event = decoder->Next(error);
                if (!event)
                    break;
                ++statusCounts[event->status & 0xF0];
                ++portCounts[event->port & 1];
                if ((event->status & 0xF0) == 0xB0)
                    ++controllerCounts[event->data1];
            }
            if (!error.empty()) {
                std::cerr << entry.path().filename().string() << " song "
                          << song << ": " << error << '\n';
                return 1;
            }
        }
    }
    if (decodedSongs == 0)
        return 1;
    if (report) {
        std::cout << decodedSongs << " songs decoded\nstatuses:";
        for (const auto& [status, count] : statusCounts)
            std::cout << " 0x" << std::hex << status << std::dec << '='
                      << count;
        std::cout << "\ncontrollers:";
        for (const auto& [controller, count] : controllerCounts)
            std::cout << " 0x" << std::hex << controller << std::dec << '='
                      << count;
        std::cout << "\nports: 0=" << portCounts[0]
                  << " 1=" << portCounts[1] << '\n';
    }
    return 0;
}
