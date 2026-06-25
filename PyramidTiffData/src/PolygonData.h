#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace PyramidTiffData
{
    // =============================================================================
	// Helper
	// =============================================================================
    
    struct Point2D {
        uint32_t x;
        uint32_t y;

        bool operator==(const Point2D&) const = default;
        bool operator!=(const Point2D&) const = default;
    };

    void rasterize_polygon(const std::vector<Point2D>& points, const uint32_t img_width, const uint32_t img_height,
        std::vector<uint32_t>& indices, std::vector<uint32_t>& pixel_counts);

    // =============================================================================
    // PolygonData
    // =============================================================================

    class PolygonData {
    public:
        PolygonData() = default;
        explicit PolygonData(const std::filesystem::path& path, const uint32_t img_width, const uint32_t img_height);
        ~PolygonData() = default;
        PolygonData(const PolygonData&) = delete;
        PolygonData& operator=(const PolygonData&) = delete;
        PolygonData(PolygonData&&) = delete;
        PolygonData& operator=(PolygonData&&) = delete;
        
        void print_info(const size_t max_polygons_to_show = 5) const;

        void init(const std::filesystem::path& path, const uint32_t img_width, const uint32_t img_height);

        std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> downscaleMask(const double scaleFactor) const;

    public: // getter
        [[nodiscard]] uint32_t img_width() const noexcept {
            return _img_width;
        }
        [[nodiscard]] uint32_t img_height() const noexcept {
            return _img_height;
        }
        [[nodiscard]] const std::vector<uint32_t>& all_positive_indices() const noexcept {
            return _all_positive_indices;
        }
        [[nodiscard]] const std::vector<uint32_t>& pixel_counts() const noexcept {
            return _pixel_counts;
        }
        [[nodiscard]] const std::vector<std::string>& names() const noexcept {
            return _names;
        }
        [[nodiscard]] const std::vector<std::array<uint8_t, 3>>& colors() const noexcept {
            return _colors;
        }

    private:
        void parse_mask_annotations(const std::filesystem::path& path);

    private:
        uint32_t _img_width{};
        uint32_t _img_height{};
        std::vector<uint32_t> _all_positive_indices{};
        std::vector<uint32_t> _pixel_counts{};
        std::vector<std::string> _names{};
        std::vector<std::array<uint8_t, 3>> _colors{};
        std::vector<std::vector<Point2D>> _polygons{};
    };

} // namespace PyramidTiffData
