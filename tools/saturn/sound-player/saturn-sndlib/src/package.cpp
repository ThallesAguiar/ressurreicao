#include <saturn_sndlib/package.hpp>

#include <algorithm>
#include <limits>

namespace saturn_sfx {

namespace {

std::uint32_t Read32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) << 24 |
           static_cast<std::uint32_t>(data[offset + 1]) << 16 |
           static_cast<std::uint32_t>(data[offset + 2]) << 8 |
           data[offset + 3];
}

void Write32(
    std::span<std::uint8_t> data, std::size_t offset, std::uint32_t value) {
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

} // namespace

std::optional<std::vector<SoundAreaMapEntry>> ParseSoundAreaMap(
    std::span<const std::uint8_t> data, std::string& error) {
    std::vector<SoundAreaMapEntry> entries;
    for (std::size_t offset = 0; offset < data.size(); offset += 8) {
        if (data[offset] == 0xFF)
            return entries;
        if (offset + 8 > data.size()) {
            error = "sound area map record is truncated";
            return std::nullopt;
        }
        const std::uint32_t sizeAndFlags = Read32(data, offset + 4);
        entries.push_back({
            data[offset],
            static_cast<std::uint32_t>(data[offset + 1]) << 16 |
                static_cast<std::uint32_t>(data[offset + 2]) << 8 |
                data[offset + 3],
            sizeAndFlags & 0x000F'FFFF,
            (sizeAndFlags & 0x8000'0000) != 0,
        });
    }
    return entries;
}

std::optional<std::vector<std::uint8_t>> PackSoundAreaMap(
    std::span<const SoundAreaMapEntry> entries, std::string& error) {
    if (entries.size() > 31) {
        error = "sound area map contains over 31 areas";
        return std::nullopt;
    }
    std::vector<std::uint8_t> result((entries.size() + 1) * 8);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (entry.id == 0xFF || entry.address > 0x00FF'FFFF ||
            entry.size > 0x000F'FFFF) {
            error = "sound area map entry is out of range";
            return std::nullopt;
        }
        const std::size_t offset = i * 8;
        result[offset] = entry.id;
        result[offset + 1] = entry.address >> 16;
        result[offset + 2] = entry.address >> 8;
        result[offset + 3] = entry.address;
        Write32(result, offset + 4,
                entry.size | (entry.loadable ? 0x8000'0000 : 0));
    }
    result[entries.size() * 8] = 0xFF;
    result[entries.size() * 8 + 1] = 0xFF;
    return result;
}

std::optional<SoundPackage> UnpackSoundPackage(
    std::span<const std::uint8_t> data,
    std::span<const std::uint8_t> areaMap, std::uint8_t baseArea,
    std::string& error) {
    const auto parsed = ParseSoundAreaMap(areaMap, error);
    if (!parsed)
        return std::nullopt;
    const auto base = std::ranges::find_if(
        *parsed, [&](const auto& area) { return area.id == baseArea; });
    if (base == parsed->end()) {
        error = "sound package base area is not present in the area map";
        return std::nullopt;
    }
    if (data.size() > std::numeric_limits<std::uint32_t>::max() -
                          base->address) {
        error = "sound package address range overflows";
        return std::nullopt;
    }
    const std::uint32_t endAddress =
        base->address + static_cast<std::uint32_t>(data.size());
    std::vector<SoundAreaMapEntry> contained;
    for (const auto& area : *parsed) {
        if (area.address >= base->address && area.address < endAddress)
            contained.push_back(area);
    }
    std::ranges::sort(contained, {}, &SoundAreaMapEntry::address);
    if (contained.empty() || contained.front().address != base->address) {
        error = "sound package does not begin on its base area";
        return std::nullopt;
    }

    SoundPackage package{baseArea, base->address, {}};
    package.areas.reserve(contained.size());
    for (std::size_t i = 0; i < contained.size(); ++i) {
        const std::size_t offset = contained[i].address - base->address;
        const std::size_t end =
            i + 1 < contained.size()
                ? contained[i + 1].address - base->address
                : data.size();
        if (end < offset || end > data.size()) {
            error = "sound package area lies outside the file";
            return std::nullopt;
        }
        package.areas.push_back({
            contained[i], offset,
            std::vector<std::uint8_t>(data.begin() + offset,
                                      data.begin() + end),
        });
    }
    return package;
}

std::optional<std::vector<std::uint8_t>> PackSoundPackage(
    const SoundPackage& package, std::string& error) {
    if (package.areas.empty()) {
        error = "sound package contains no areas";
        return std::nullopt;
    }
    std::vector<const SoundPackageArea*> areas;
    areas.reserve(package.areas.size());
    for (const auto& area : package.areas)
        areas.push_back(&area);
    std::ranges::sort(areas, {}, [](const auto* area) { return area->offset; });
    if (areas.front()->offset != 0 ||
        areas.front()->map.id != package.baseArea ||
        areas.front()->map.address != package.baseAddress) {
        error = "sound package has an invalid base area";
        return std::nullopt;
    }

    std::size_t size = 0;
    for (const auto* area : areas) {
        if (area->offset > std::numeric_limits<std::size_t>::max() -
                               area->data.size()) {
            error = "sound package size overflows";
            return std::nullopt;
        }
        const std::size_t end = area->offset + area->data.size();
        if (area->offset < size) {
            error = "sound package areas overlap";
            return std::nullopt;
        }
        size = end;
    }
    std::vector<std::uint8_t> result(size);
    for (const auto* area : areas) {
        std::ranges::copy(area->data, result.begin() + area->offset);
    }
    return result;
}

} // namespace saturn_sfx
