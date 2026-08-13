#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PyramidTiffData {

    struct Point2D {
        double x;
        double y;

        bool operator==(const Point2D&) const = default;
        bool operator!=(const Point2D&) const = default;
    };

    struct Rect {
        uint32_t x, y, w, h;

        bool operator==(const Rect&) const = default;
        bool operator!=(const Rect&) const = default;
    };

    enum class MaskType : uint8_t
    {
        Roi,
        Tissue,
        Cell,
        Nucleus,    // subset of cell, usually handled as part of cell
        None,
    };

    std::string getMaskString(const MaskType maskType);

    void sortAndUnique(std::vector<uint32_t>& v);

    std::vector<uint32_t> convertSelectionToDownscaled(
        const std::vector<uint32_t>& selectedIndices,
        const uint32_t originalWidth, const uint32_t originalHeight,
        const uint32_t newWidth, const uint32_t newHeight);

    std::vector<uint32_t> convertSelectionToUpscaled(
        const std::vector<uint32_t>& selectedIndices,
        const uint32_t originalWidth, const uint32_t originalHeight,
        const uint32_t newWidth, const uint32_t newHeight);

    std::vector<uint32_t> rasterize_polygon(const std::vector<Point2D>& points,
        const uint32_t img_width, const uint32_t img_height);

    // append rasterized output of rasterize_polygon to indices
    void rasterize_polygon(const std::vector<Point2D>& points,
        const uint32_t img_width, const uint32_t img_height,
        std::vector<uint32_t>& indices, std::vector<uint32_t>& pixel_counts);

    Point2D computeCentroid(const std::vector<Point2D>& coordinates);

    void ProgressBarPrint(const std::uintmax_t current, std::uintmax_t& previous_pct, const std::uintmax_t total);

    void ProgressBarFinish();

    inline std::uintmax_t ProgressBarInit() { return 0; }

}
