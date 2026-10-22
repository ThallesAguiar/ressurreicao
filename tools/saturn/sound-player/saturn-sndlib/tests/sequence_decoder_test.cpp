#include <saturn_sndlib/sequence.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

bool Check(std::uint8_t song, const saturn_sfx::SequenceEvent& expected) {
    const auto bank = Read(std::filesystem::path(SATURN_SNDLIB_SOTN_GAME_DIR) /
                           "SD/SD_JO10.CRT");
    std::string error;
    auto decoder = saturn_sfx::SequenceDecoder::Open(bank, song, error);
    if (!decoder) {
        std::cerr << error << '\n';
        return false;
    }
    const auto event = decoder->Next(error);
    if (!event || *event != expected) {
        std::cerr << "song " << static_cast<unsigned>(song)
                  << " first event mismatch: " << error << '\n';
        return false;
    }
    return decoder->Resolution() == 480 &&
           decoder->InitialTempo().microsecondsPerQuarter == 500000;
}

bool CheckCommonBank() {
    const auto bank = Read(std::filesystem::path(SATURN_SNDLIB_SOTN_GAME_DIR) /
                           "SD/SD_JO10.CRT");
    if (bank.size() < 2)
        return false;
    const unsigned songs = static_cast<unsigned>(bank[0]) << 8 | bank[1];
    for (unsigned song = 0; song < songs; ++song) {
        std::string error;
        auto decoder = saturn_sfx::SequenceDecoder::Open(bank, song, error);
        if (!decoder) {
            std::cerr << "song " << song << ": " << error << '\n';
            return false;
        }
        for (unsigned event = 0; event < 256; ++event) {
            if (!decoder->Next(error))
                break;
        }
        if (!error.empty()) {
            std::cerr << "song " << song << ": " << error << '\n';
            return false;
        }
    }
    return true;
}

bool CheckLoopDetection() {
    const std::vector<std::uint8_t> bank{
        0x00, 0x01,             // one song
        0x00, 0x00, 0x00, 0x06, // song offset
        0x01, 0xE0, 0x00, 0x01, // resolution, track count
        0x00, 0x10, 0x00, 0x08, // track and tempo offsets
        0x00, 0x00, 0x00, 0x00, // initial tempo delta
        0x00, 0x07, 0xA1, 0x20, // 500000 us/quarter
        0x82, 0x03,             // loop start
        0x00, 60, 100, 5, 7,   // note
        0x82, 0x0B,             // loop end and backward branch
    };
    std::string error;
    auto decoder = saturn_sfx::SequenceDecoder::Open(bank, 0, error);
    if (!decoder)
        return false;
    const auto first = decoder->Next(error);
    const auto second = decoder->Next(error);
    return error.empty() && first && second && first->delta == 10 &&
           second->delta == 18 && decoder->LoopCount() == 1 &&
           decoder->LastLoopDelta() &&
           *decoder->LastLoopDelta() == 11;
}

} // namespace

int main() {
    using saturn_sfx::SequenceEvent;
    return Check(0, SequenceEvent{0, 0, 0xB0, 0, 0, 0}) &&
                   Check(1, SequenceEvent{0, 0, 0xB0, 0, 0, 0}) &&
                   CheckCommonBank() && CheckLoopDetection()
               ? 0
               : 1;
}
