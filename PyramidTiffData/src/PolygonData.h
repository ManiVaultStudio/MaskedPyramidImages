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

    void rasterize_polygon(const std::vector<Point2D>& points, 
        const uint32_t img_width, const uint32_t img_height,
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

        std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> downscaleMaskRoi(const double scaleFactor) const;
        std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> downscaleMaskTissue(const double scaleFactor) const;

    public: // getter
        [[nodiscard]] uint32_t img_width() const noexcept {
            return _img_width;
        }
        [[nodiscard]] uint32_t img_height() const noexcept {
            return _img_height;
        }
        [[nodiscard]] const std::vector<std::string>& names_roi() const noexcept {
            return _names_roi;
        }
        [[nodiscard]] const std::vector<std::array<uint8_t, 3>>& colors_roi() const noexcept {
            return _colors_roi;
        }
        [[nodiscard]] const std::vector<std::string>& names_tissue() const noexcept {
            return _names_tissue;
        }
        [[nodiscard]] bool has_roi() const noexcept {
            return !_polygons_roi.empty();
        }
        [[nodiscard]] bool has_tissue() const noexcept {
            return !_polygons_tissue.empty();
        }

    private:
        void parse_mask_annotations(const std::filesystem::path& path);
        std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> downscaleMask(const double scaleFactor, const std::vector<std::vector<Point2D>>& polygons_roi) const;

    private:
        uint32_t _img_width{};
        uint32_t _img_height{};

        std::vector<std::string> _names_roi{};
        std::vector<std::string> _names_tissue{};

        std::vector<std::array<uint8_t, 3>> _colors_roi{};
        std::vector<std::array<uint8_t, 3>> _colors_tissue{};

        std::vector<std::vector<Point2D>> _polygons_roi{};
        std::vector<std::vector<Point2D>> _polygons_tissue{};
        std::vector<std::vector<Point2D>> _polygons_membrane{};     // not yet used
    };

} // namespace PyramidTiffData
