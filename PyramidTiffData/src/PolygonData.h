#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "CommonTypesAndTransformations.h"

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
        
        void printInfo(const size_t max_polygons_to_show = 5) const;

        void init(const std::filesystem::path& path, const uint32_t img_width, const uint32_t img_height);

        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getMaskRoi(const double scaleFactorWidth, const double scaleFactorHeight, const uint32_t imgWidthScaled, const uint32_t imgHeightScaled) const;
        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getMaskTissue(const double scaleFactorWidth, const double scaleFactorHeight, const uint32_t imgWidthScaled, const uint32_t imgHeightScaled) const;
        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getMaskCell(const double scaleFactorWidth, const double scaleFactorHeight, const uint32_t imgWidthScaled, const uint32_t imgHeightScaled) const;
        [[nodiscard]] std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> getMaskNucleus(const double scaleFactorWidth, const double scaleFactorHeight, const uint32_t imgWidthScaled, const uint32_t imgHeightScaled) const;

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
        [[nodiscard]] const std::vector<Point2D>& centroids_cell() const noexcept {
            return _centroids_cell;
        }
        [[nodiscard]] const std::vector<size_t>& roi_nums_cell() const noexcept {
            return _roi_nums_cell;
        }
        [[nodiscard]] const std::string& roi_name_cell(size_t cellNum) const noexcept {
            return _names_roi[_roi_nums_cell[cellNum]];
        }
        [[nodiscard]] const std::vector<std::string>& names_measurements() const noexcept {
            return _names_measurements;
        }
        [[nodiscard]] const std::vector<float>& means_nucleus() const noexcept {
            return _means_nucleus;
        }
        [[nodiscard]] const std::vector<float>& means_cytoplasm() const noexcept {
            return _means_cytoplasm;
        }
        [[nodiscard]] const std::vector<float>& means_membrane() const noexcept {
            return _means_membrane;
        }
        [[nodiscard]] const std::vector<float>& means_cell() const noexcept {
            return _means_cell;
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
        [[nodiscard]] bool has_means() const noexcept {
            return !_means_nucleus.empty()
                && _means_nucleus.size() == _means_cytoplasm.size()
                && _means_cytoplasm.size() == _means_membrane.size()
                && _means_membrane.size() == _means_cell.size();
        }

    private:
        void parseMaskAnnotations(const std::filesystem::path& path);

        [[nodiscard]] static std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> downscaleMask(
            const double scaleFactorWidth, const double scaleFactorHeight,
            const uint32_t imgWidthScaled, const uint32_t imgHeightScaled,
            const std::vector<std::vector<Point2D>>& polygons);

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

        std::vector<Point2D> _centroids_cell{};
        std::vector<size_t> _roi_nums_cell{};

        std::vector<std::string> _names_measurements{};
        std::vector<float> _means_nucleus{};
        std::vector<float> _means_cytoplasm{};
        std::vector<float> _means_membrane{};
        std::vector<float> _means_cell{};
    };

} // namespace PyramidTiffData
