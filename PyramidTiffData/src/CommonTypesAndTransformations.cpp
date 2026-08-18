#include "CommonTypesAndTransformations.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>  // for memset

#include <fmt/base.h>

#ifdef __cpp_lib_execution
#ifdef __GNUC__  // both TBB and Qt define emit keyword: undef
#undef emit
#endif
#include <execution>
#ifdef __GNUC__ // both TBB and Qt define emit keyword: def again
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

    std::string getMaskString(const MaskType maskType)
    {
        switch (maskType)
        {
        case MaskType::Roi: return "ROI";
        case MaskType::Tissue: return "TISSUE";
        case MaskType::Cell: return "CELL";
        case MaskType::Nucleus: return "NUCLEUS";
        case MaskType::None: return "NONE";
        }

        return "None";
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

    std::vector<uint32_t> rasterize_polygon(
        const std::vector<Point2D>& points, const uint32_t img_width, const uint32_t img_height)
    {
        if (points.empty()) return {};
        if (points.front() != points.back()) return {};

        // Find bounding box to limit search area
        auto [minIt, maxIt] = std::minmax_element(MV_PYRAMID_PARALLEL_EXECUTION
            points.begin(), points.end(),
            [](const Point2D& a, const Point2D& b)
            { return a.y < b.y; });
        const double minY = minIt->y;
        const double maxY = maxIt->y;

        const auto img_width_d = static_cast<double>(img_width);
        const auto max_id = static_cast<uint64_t>(img_width) * img_height;

        std::vector<uint32_t> indices;

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
                    nodes.push_back(static_cast<uint32_t>(std::round(clampedX)));
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

        sortAndUnique(indices);

        return indices;
    }

    void rasterize_polygon(const std::vector<Point2D>& points, const uint32_t img_width, const uint32_t img_height,
        std::vector<uint32_t>& indices, std::vector<uint32_t>& pixel_counts)
    {
        if (points.empty()) return;
        if (points.front() != points.back()) return;

        std::vector<uint32_t> local_indices = rasterize_polygon(points, img_width, img_height);

        pixel_counts.push_back(static_cast<uint32_t>(local_indices.size()));

        indices.reserve(indices.size() + local_indices.size());
        indices.insert(indices.end(),
            std::make_move_iterator(local_indices.begin()),
            std::make_move_iterator(local_indices.end()));
    }

    // Computes the area-weighted centroid of a simple polygon.
    // https://en.wikipedia.org/wiki/Centroid#Of_a_polygon
    Point2D computeCentroid(const std::vector<Point2D>& coordinates)
    {
        const std::size_t n = coordinates.size();

        // Need at least 3 distinct vertices + closing point => size >= 4
        if (n < 4) {
            fmt::println("computeCentroid: need at least a closed triangle (>=4 points).");
            return {};
        }
        if (coordinates.front() != coordinates.back()) {
            fmt::println("computeCentroid: polygon must be closed (first == last point).");
            return {};
        }

        double signed_area = 0.0;
        double cx = 0.0;
        double cy = 0.0;

        // Iterate over edges (p_i, p_{i+1}); last edge is (p_{n-2}, p_{n-1}==p_0)
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const Point2D& p0 = coordinates[i];
            const Point2D& p1 = coordinates[i + 1];

            const double cross = p0.x * p1.y - p1.x * p0.y;
            signed_area += cross;
            cx += (p0.x + p1.x) * cross;
            cy += (p0.y + p1.y) * cross;
        }

        signed_area *= 0.5;

        // Degenerate polygon (zero area, e.g. collinear points): fall back
        // to the simple average of the (unique) vertices.
        if (std::abs(signed_area) < 1e-12) {
            double sum_x = 0.0, sum_y = 0.0;
            const std::size_t unique_count = n - 1; // exclude repeated closing vertex
            for (std::size_t i = 0; i < unique_count; ++i) {
                sum_x += coordinates[i].x;
                sum_y += coordinates[i].y;
            }
            return Point2D{ sum_x / static_cast<double>(unique_count),
                             sum_y / static_cast<double>(unique_count) };
        }

        const double factor = 1.0 / (6.0 * signed_area);
        return Point2D{ cx * factor, cy * factor };
    }


    constexpr uintmax_t ProgressBarWidth = 40;

    void ProgressBarPrint(const std::uintmax_t current, std::uintmax_t& previous_pct, const std::uintmax_t total)
    {
        const uintmax_t pct = static_cast<uintmax_t>((current * 100) / total);
        if (pct != previous_pct) {
            previous_pct = pct;
            const int filled = static_cast<int>(static_cast<double>(ProgressBarWidth * pct) / 100.0);
            fmt::print("\r[{:=<{}}{: <{}}] {:3}%", "", filled, "", ProgressBarWidth - filled, pct);
            [[maybe_unused]] int success = std::fflush(stdout);
        }

    }

    void ProgressBarFinish()
    {
        fmt::print("\r[{:=<{}}{: <{}}] {:3}%\n", "", ProgressBarWidth, "", 0, 100.0); // 100%
    }

} // PyramidTiffData
