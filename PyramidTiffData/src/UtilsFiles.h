#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace PyramidTiffData
{
    class PolygonData;
}

namespace PyramidTiffData {
    std::filesystem::path changeExtension(
        const std::filesystem::path& p,
        const std::string_view ext);

    std::filesystem::path insertSuffixExtension(
        const std::filesystem::path& p,
        const std::string& suffix);

    void writeClusterIdsToCsv(const std::filesystem::path& p, const int64_t numCells, const int64_t numClusters,
        const PolygonData& polygons, const std::vector<std::vector<uint32_t>>& cellClusterIds);
}
