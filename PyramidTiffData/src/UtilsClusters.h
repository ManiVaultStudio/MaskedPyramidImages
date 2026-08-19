#pragma once

#include "CommonTypesAndTransformations.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PyramidTiffData {
    struct CellStruct
    {
       Point2D centroid{};
       std::array<uint32_t, 4> basePixelsBounds{};
       std::vector<uint32_t> basePixels{};
       std::string cellName{};
       std::string imageName{};
       std::vector<uint32_t> clusterIds = {};
    };

    std::vector<CellStruct> readCellStructs(const std::filesystem::path& jsonFilePath,
       const uint32_t baseWidth, const uint32_t baseHeight, bool flip = false);

}
