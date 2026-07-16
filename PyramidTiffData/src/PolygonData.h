#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "UtilsTransform.h"

namespace PyramidTiffData
{
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

        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getMaskRoi(const double scaleFactor) const;
        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getMaskTissue(const double scaleFactor) const;
        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getMaskCell(const double scaleFactor) const;
        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getMaskNucleus(const double scaleFactor) const;

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
        [[nodiscard]] const std::vector<std::string>& names_tissue() const noexcept {
            return _names_tissue;
        }
        [[nodiscard]] const std::vector<std::string>& names_cell() const noexcept {
            return _names_cell;
        }
        [[nodiscard]] const std::vector<std::array<uint8_t, 3>>& colors_roi() const noexcept {
            return _colors_roi;
        }
        [[nodiscard]] const std::vector<std::array<uint8_t, 3>>& colors_tissue() const noexcept {
            return _colors_tissue;
        }
        [[nodiscard]] bool has_roi() const noexcept {
            return !_polygons_roi.empty();
        }
        [[nodiscard]] bool has_tissue() const noexcept {
            return !_polygons_tissue.empty();
        }
        [[nodiscard]] bool has_cell() const noexcept {
            return !_polygons_cell.empty();
        }
        [[nodiscard]] bool has_nucleus() const noexcept {
            return !_polygons_nucleus.empty();
        }

    private:
        void parse_mask_annotations(const std::filesystem::path& path);
        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> downscaleMask(const double scaleFactor, const std::vector<std::vector<Point2D>>& polygons_roi) const;

    private:
        uint32_t _img_width{};
        uint32_t _img_height{};

        std::vector<std::string> _names_roi{};
        std::vector<std::string> _names_tissue{};
        std::vector<std::string> _names_cell{};

        std::vector<std::array<uint8_t, 3>> _colors_roi{};
        std::vector<std::array<uint8_t, 3>> _colors_tissue{};

        std::vector<std::vector<Point2D>> _polygons_roi{};
        std::vector<std::vector<Point2D>> _polygons_tissue{};
        std::vector<std::vector<Point2D>> _polygons_cell{};
        std::vector<std::vector<Point2D>> _polygons_nucleus{};
    };

} // namespace PyramidTiffData
