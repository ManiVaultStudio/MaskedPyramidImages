#pragma once

#include <string>
#include <string_view>
#include <filesystem>

namespace PyramidTiffData {
    std::filesystem::path changeExtension(
        const std::filesystem::path& p,
        const std::string_view ext);

    std::filesystem::path insertSuffixExtension(
        const std::filesystem::path& p,
        const std::string& suffix);
}
