#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace imageview {
struct Metadata {
    int orientation = 1;
    std::vector<std::pair<std::string,std::string>> fields;
    std::vector<std::uint8_t> icc;
};
// Read only bounded metadata records, never decode pixel data.
Metadata readMetadata(std::span<const std::uint8_t> bytes);
Metadata readExif(std::span<const std::uint8_t> bytes);
}
