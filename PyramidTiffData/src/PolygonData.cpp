#include "PolygonData.h"

#include "UtilsJson.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifndef NDEBUG
#include <numeric>
#endif

#include <fmt/base.h>
#include <fmt/std.h>

#include <jsoncons/json.hpp>
#include <jsoncons/json_cursor.hpp>

namespace PyramidTiffData {

    // =============================================================================
	// PolygonData
	// =============================================================================

    PolygonData::PolygonData(const std::filesystem::path& path, const uint32_t img_width, const uint32_t img_height)
    {
        init(path, img_width, img_height);
    }

    void PolygonData::init(const std::filesystem::path& path, const uint32_t img_width, const uint32_t img_height)
    {
        _img_width = img_width;
        _img_height = img_height;

        parseMaskAnnotations(path);
    }

    void PolygonData::parseMaskAnnotations(const std::filesystem::path& path)
    {
        std::ifstream f(path);
        if (!f.is_open()) {
            fmt::println("PolygonData::parse_mask_annotations: could not open {}", path.string());
            return;
        }

        bool found_features = false;

        try {
            jsoncons::json_stream_cursor cursor(f);

            int64_t roi_counter = 0;            // ROI is either before all cells or after
            bool roi_before_cells = true;

            int64_t unnamed_roi_counter = 0;
            int64_t unnamed_tissue_counter = 0;
            int64_t unnamed_cell_counter = 0;
            const std::string prefix_roi = getMaskString(MaskType::Roi);
            const std::string prefix_tissue = getMaskString(MaskType::Tissue);
            const std::string prefix_cell = getMaskString(MaskType::Cell);
            
            bool in_features_array = false;

            const uintmax_t total_bytes = std::filesystem::file_size(path);
            jsoncons::json_decoder<jsoncons::ojson> decoder;

            fmt::println("Reading the json file...");

            auto last_pct = ProgressBarInit();
            for (; !cursor.done(); cursor.next())
            {
                const auto& event = cursor.current();
                switch (event.event_type())
                {
                case jsoncons::staj_event_type::key:
                {
                    const auto key = event.get<jsoncons::string_view>();
                    if (key == "features") {
                        in_features_array = true;
                        found_features = true;
                    }
                    break;
                }
                case jsoncons::staj_event_type::begin_object:
                {
                    if (!in_features_array) {
                        break; // e.g. the document's own root object
                    }

                    // Pull exactly one "feature" object into memory, process it,
                    // then let it go out of scope, the rest of the file stays unread.
                    cursor.read_to(decoder);
                    const jsoncons::ojson feature = decoder.get_result();

                    const auto maskType = getMaskType(feature);

                    if (maskType == MaskType::Roi)
                    {
                        roi_counter++;
                        parseName(feature, _names_roi, prefix_roi, unnamed_roi_counter);
                        parseGeometry(feature, _polygons_roi);
                        parseColor(feature, _colors_roi);
                    }
                    else if (maskType == MaskType::Tissue)
                    {
                        parseNameID(feature, _names_tissue, prefix_tissue, unnamed_tissue_counter);
                        parseGeometry(feature, _polygons_tissue);
                        parseColor(feature, _colors_tissue);
                    }
                    else if (maskType == MaskType::Cell)
                    {
                        if (roi_counter == 0 && _names_cell.empty())
                            roi_before_cells = false;

                        parseNameID(feature, _names_cell, prefix_cell, unnamed_cell_counter);
                        parseGeometry(feature, _polygons_cell);
                        parseGeometryNucleus(feature, _polygons_nucleus);

                        _centroids_cell.push_back(computeCentroid(_polygons_cell.back()));
                        _roi_nums_cell.push_back(roi_before_cells ? roi_counter - 1 : roi_before_cells);

                        parseMeasurementNames(feature, _names_measurements);
                        parseMeasurementMeans(feature, _means_nucleus, "Nucleus");
                        parseMeasurementMeans(feature, _means_cytoplasm, "Cytoplasm");
                        parseMeasurementMeans(feature, _means_membrane, "Membrane");
                        parseMeasurementMeans(feature, _means_cell, "Cell");
                    }
                    else
                    {
                        fmt::println("PolygonData::parse_mask_annotations: json feature does not contain ROI, TISSUE or CELL");
                    }

                    break;
                }
                case jsoncons::staj_event_type::end_array:
                {
                    if (in_features_array) {
                        in_features_array = false;
                    }
                    break;
                }
                default:
                    break;
                }

                const auto pos = f.tellg();
                if (pos > 0) ProgressBarPrint(static_cast<std::uintmax_t>(pos), last_pct, total_bytes);
            }
            ProgressBarFinish();
        }
        catch (const std::exception& err) {
            fmt::println("PolygonData::parse_mask_annotations: json parse error: {}", err.what());
            return;
        }

        f.close();

        if (!found_features) {
            fmt::println("PolygonData::parse_mask_annotations: json does not contain features field");
            return;
        }

        assert(_names_roi.size() == _colors_roi.size());
        assert(_colors_roi.size() == _polygons_roi.size());
        assert(_polygons_tissue.empty() || _polygons_roi.size() == _polygons_tissue.size());
        assert(_polygons_tissue.empty() || _colors_roi.size() == _polygons_tissue.size());
        assert((_polygons_cell.empty() || _polygons_nucleus.empty()) || _polygons_cell.size() == _polygons_nucleus.size());
        assert(_centroids_cell.empty() || _polygons_cell.size() == _centroids_cell.size());
        assert(_names_measurements.empty() || _means_nucleus.size() % _names_measurements.size() == 0);
        assert(_means_nucleus.size() == _means_cytoplasm.size());
        assert(_means_cytoplasm.size() == _means_membrane.size());
        assert(_means_membrane.size() == _means_cell.size());
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::getMaskRoi(const double scaleFactorWidth, const double scaleFactorHeight, const uint32_t imgWidthScaled, const uint32_t imgHeightScaled) const
    {
        return downscaleMask(scaleFactorWidth, scaleFactorHeight, imgWidthScaled, imgHeightScaled, _polygons_roi);
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::getMaskTissue(const double scaleFactorWidth, const double scaleFactorHeight, const uint32_t imgWidthScaled, const uint32_t imgHeightScaled) const
    {
        return downscaleMask(scaleFactorWidth, scaleFactorHeight, imgWidthScaled, imgHeightScaled, _polygons_tissue);
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::getMaskCell(const double scaleFactorWidth, const double scaleFactorHeight, const uint32_t imgWidthScaled, const uint32_t imgHeightScaled) const
    {
        return downscaleMask(scaleFactorWidth, scaleFactorHeight, imgWidthScaled, imgHeightScaled, _polygons_cell);
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::getMaskNucleus(const double scaleFactorWidth, const double scaleFactorHeight, const uint32_t imgWidthScaled, const uint32_t imgHeightScaled) const
    {
        return downscaleMask(scaleFactorWidth, scaleFactorHeight, imgWidthScaled, imgHeightScaled, _polygons_nucleus);
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::downscaleMask(const double scaleFactorWidth, const double scaleFactorHeight,
        const uint32_t imgWidthScaled, const uint32_t imgHeightScaled,
        const std::vector<std::vector<Point2D>>& polygons)
    {
        std::vector<uint32_t> indices{};
        std::vector<uint32_t> pixelCounts{};

        auto scale_coords = [scaleFactorWidth, scaleFactorHeight](const std::vector<Point2D>& points) -> std::vector<Point2D> {
            std::vector<Point2D> pointsScaled(points.size());

            const int64_t numPoints = static_cast<int64_t>(points.size());
#pragma omp parallel for
            for (int64_t i = 0; i < numPoints; ++i) {
                pointsScaled[i] = {
                    .x = std::round(points[i].x * scaleFactorWidth),
                    .y = std::round(points[i].y * scaleFactorHeight)
                };
            }

            return pointsScaled;
            };

        auto lastPct = ProgressBarInit();
        std::uintmax_t currentID = 0;
        for (const auto& coords : polygons) {
            const auto& coords_scaled = (scaleFactorWidth == 1.0) ? coords : scale_coords(coords);
            rasterize_polygon(coords_scaled, imgWidthScaled, imgHeightScaled, indices, pixelCounts);

            ProgressBarPrint(currentID++, lastPct, polygons.size());
        }
        ProgressBarFinish();

        flipMaskIDs(indices, imgWidthScaled, imgHeightScaled);

        assert(indices.size() == std::accumulate(pixelCounts.begin(), pixelCounts.end(), 0ull));

        return { indices , pixelCounts };
    }

    void PolygonData::printInfo(const size_t max_polygons_to_show ) const
    {
        fmt::print("PolygonData Information");
    	fmt::print("Image Dimensions: {}x{}\n", _img_width, _img_height);
        fmt::print("Total Polygons Detected: {}\n", _names_roi.size());

        // Print details for a limited number of polygons
        const size_t polygons_to_show = std::min(_names_roi.size(), max_polygons_to_show);

        fmt::print("\n--- Polygon Details ({}/{} shown) ---\n",
            polygons_to_show, _names_roi.size());

        for (size_t i = 0; i < polygons_to_show; ++i) {
            fmt::print("  - Polygon {}: Name='{}', Color=({}, {}, {})\n",
                i + 1,
                _names_roi[i],
                _colors_roi[i][0], _colors_roi[i][1], _colors_roi[i][2]);
        }

        if (_names_roi.size() > max_polygons_to_show) {
            fmt::print("  ... {} more polygons not shown.\n", _names_roi.size() - max_polygons_to_show);
        }

        fmt::print("{:-<60}\n", "");
    }


} // namespace PyramidTiffData
