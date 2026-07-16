#pragma once

#include <cstdint>
#include <vector>

namespace PyramidTiffData {

    struct Point2D {
        uint32_t x;
        uint32_t y;

        bool operator==(const Point2D&) const = default;
        bool operator!=(const Point2D&) const = default;
    };

    enum class MaskType : uint8_t
    {
        Roi,
        Tissue,
        Cell,
        None,
    };

    void sortAndUnique(std::vector<uint32_t>& v);

    std::vector<uint32_t> convertSelectionToDownscaled(
        const std::vector<uint32_t>& selectedIndices,
        const uint32_t originalWidth, const uint32_t originalHeight,
        const uint32_t newWidth, const uint32_t newHeight);

    std::vector<uint32_t> convertSelectionToUpscaled(
        const std::vector<uint32_t>& selectedIndices,
        const uint32_t originalWidth, const uint32_t originalHeight,
        const uint32_t newWidth, const uint32_t newHeight);

    void rasterize_polygon(const std::vector<Point2D>& points, 
        const uint32_t img_width, const uint32_t img_height,
        std::vector<uint32_t>& indices, std::vector<uint32_t>& pixel_counts);

}