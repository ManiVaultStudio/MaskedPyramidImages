#include "UtilsTransform.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>  // for memset
#include <filesystem>

#if defined(__cpp_lib_execution)
#if defined(__GNUC__)  // both TBB and Qt define emit keyword: undef
#undef emit
#endif
#include <execution>
#if defined(__GNUC__) // both TBB and Qt define emit keyword: def again
#define emit
#endif
#ifdef NDEBUG
#define MV_PYRAMID_PARALLEL_EXECUTION std::execution::par,
#else
#define MV_PYRAMID_PARALLEL_EXECUTION std::execution::seq,
#endif
#else
#define MV_PYRAMID_PARALLEL_EXECUTION
#endif

namespace PyramidTiffData {

    void sortAndUnique(std::vector<uint32_t>& v)
    {
        if (v.size() <= 1)
            return;

        std::sort(MV_PYRAMID_PARALLEL_EXECUTION
            v.begin(),
            v.end());
        const auto last = std::unique(MV_PYRAMID_PARALLEL_EXECUTION
            v.begin(),
            v.end());
        v.erase(last, v.end());
    }

    std::vector<uint32_t> convertSelectionToDownscaled(
        const std::vector<uint32_t>& selectedIndices,
        const uint32_t originalWidth, const uint32_t originalHeight,
        const uint32_t newWidth, const uint32_t newHeight)
    {
        const double scaleFactor = static_cast<double>(newWidth) / static_cast<double>(originalWidth);
        assert(scaleFactor <= 1.0);

        const uint32_t newSize = newWidth * newHeight;
        std::vector<uint8_t> bitmap(newSize, 0);

        uint32_t prevIdx = 0;
        uint32_t x = 0;
        uint32_t y = 0;

        for (const uint32_t idx : selectedIndices) {
            const uint32_t delta = idx - prevIdx;
            x += delta;
            while (x >= originalWidth) {
                x -= originalWidth; ++y;
            }
            prevIdx = idx;

            const uint32_t newX = static_cast<uint32_t>(static_cast<double>(x) * scaleFactor);
            const uint32_t newY = static_cast<uint32_t>(static_cast<double>(y) * scaleFactor);

            bitmap[newY * newWidth + newX] = 1;
        }

        std::vector<uint32_t> result;
        result.reserve(newSize); // upper bound
        for (uint32_t i = 0; i < newSize; ++i)
            if (bitmap[i]) result.push_back(i);
        result.shrink_to_fit();
        return result;
    }

    std::vector<uint32_t> convertSelectionToUpscaled(
        const std::vector<uint32_t>& selectedIndices,
        const uint32_t originalWidth, const uint32_t originalHeight,
        const uint32_t newWidth, const uint32_t newHeight)
    {
        if (selectedIndices.empty())
            return {};

        const double scaleFactor = static_cast<double>(newWidth) / static_cast<double>(originalWidth);
        assert(scaleFactor >= 1.0);

        const uint32_t newSize = newWidth * newHeight;
        std::vector<uint8_t> bitmap(newSize, 0);

        uint32_t prevIdx = 0;
        uint32_t x = 0, y = 0;

        for (const uint32_t idx : selectedIndices) {
            const uint32_t delta = idx - prevIdx;
            x += delta;
            while (x >= originalWidth) { x -= originalWidth; ++y; }
            prevIdx = idx;

            // Expand each pixel into a d by d block
            // The inverse of floor(newX / scaleFactor) == x
            // is the range: [x * scaleFactor, (x+1) * scaleFactor)
            const uint32_t newXStart = static_cast<uint32_t>(std::floor(x * scaleFactor));
            const uint32_t newXEnd = std::min(static_cast<uint32_t>(std::floor((x + 1) * scaleFactor)), newWidth);
            const uint32_t newYStart = static_cast<uint32_t>(std::floor(y * scaleFactor));
            const uint32_t newYEnd = std::min(static_cast<uint32_t>(std::floor((y + 1) * scaleFactor)), newHeight);

            for (uint32_t ny = newYStart; ny < newYEnd; ++ny)
                std::memset(&bitmap[ny * newWidth + newXStart], 1, newXEnd - newXStart);
        }

        std::vector<uint32_t> result;
        result.reserve(newSize); // upper bound
        for (uint32_t i = 0; i < newSize; ++i)
            if (bitmap[i]) result.push_back(i);
        result.shrink_to_fit();
        return result;
    }

    void rasterize_polygon(const std::vector<Point2D>& points, const uint32_t img_width, const uint32_t img_height,
        std::vector<uint32_t>& indices, std::vector<uint32_t>& pixel_counts)
    {
        if (points.empty()) return;
        if (points.front() != points.back()) return;

        // Find bounding box to limit search area
        auto [minIt, maxIt] = std::minmax_element(MV_PYRAMID_PARALLEL_EXECUTION
			points.begin(), points.end(),
            [](const Point2D& a, const Point2D& b) 
            { return a.y < b.y; });
        const double minY = minIt->y;
        const double maxY = maxIt->y;

        const uint32_t count_before = static_cast<uint32_t>(indices.size());
        const auto img_width_d = static_cast<double>(img_width);
        const auto max_id = static_cast<uint64_t>(img_width) * img_height;

        // Iterate through each scanline
        for (uint32_t y = static_cast<uint32_t>(minY); y <= maxY; ++y) {
            const double scanline = static_cast<double>(y) + 0.5; // pixel center

            std::vector<uint32_t> nodes;
            size_t j = points.size() - 1;

            // Find intersections of the scanline with polygon edges
            for (size_t i = 0; i < points.size(); ++i) {
                const auto& [xi, yi] = points[i];
                const auto& [xj, yj] = points[j];

                if ((yi <= scanline && yj > scanline) || (yj <= scanline && yi > scanline)) {
                	const double nodeX = xi + (scanline - yi) / (yj - yi) * (xj - xi);
                    const double clampedX = std::clamp(nodeX, 0.0, img_width_d - 1.0);
                    nodes.push_back(static_cast<uint32_t>(std::lround(clampedX)));
                }
                j = i;
            }

            std::ranges::sort(nodes);

            // Fill pixels between pairs of nodes (Even-Odd rule)
            for (size_t i = 0; i < nodes.size(); i += 2) {
                if (i + 1 >= nodes.size()) break;

                const uint32_t leftX = nodes[i];
                const uint32_t rightX = nodes[i + 1];

                for (uint32_t x = leftX; x < rightX; ++x) {
                    // Convert 2D to 1D index
                    if (const uint64_t idx = static_cast<uint64_t>(y) * img_width + x;
                        idx < max_id)
                        indices.push_back(static_cast<uint32_t>(idx));
                }
            }
        }

        const uint32_t count_after = static_cast<uint32_t>(indices.size());
        pixel_counts.push_back(count_after - count_before);

        indices.shrink_to_fit();
        pixel_counts.shrink_to_fit();
    }

} // PyramidTiffData
