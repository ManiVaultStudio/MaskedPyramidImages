#pragma once

#include "CommonTypesAndTransformations.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <ankerl/unordered_dense.h>

namespace PyramidTiffData {
    struct CellStruct
    {
       Point2D centroid{};
       std::vector<uint32_t> basePixels{};
       std::string cellName{};
       std::string imageName{};
       int64_t clusterId = -1;
    };

    std::vector<CellStruct> readCellStructs(const std::filesystem::path& jsonFilePath,
       const uint32_t baseWidth, const uint32_t baseHeight);

}
