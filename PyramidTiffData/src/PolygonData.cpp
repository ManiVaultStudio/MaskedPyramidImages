#include "PolygonData.h"

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

namespace jsoncons {
    template<class Json>
    struct json_type_traits<Json, PyramidTiffData::Point2D> {
        static bool is(const Json& j) {
            return j.is_array() && j.size() >= 2 &&
                j[0].is_number() && j[1].is_number();
        }

        static PyramidTiffData::Point2D as(const Json& j) {
            // Coordinates can carry sub-pixel precision (e.g. 10562.5)
            return {
                .x = j[0].as_double(),
                .y = j[1].as_double()
            };
        }

        static Json to_json(const PyramidTiffData::Point2D& p) {
            Json j(json_array_arg);
            j.push_back(p.x);
            j.push_back(p.y);
            return j;
        }
    };
}

namespace PyramidTiffData {

    // =============================================================================
    // Helper
    // =============================================================================

    constexpr uintmax_t ProgressBarWidth = 40;

    inline void ProgressBarPrint(const std::uintmax_t current, std::uintmax_t& previous_pct, const std::uintmax_t total)
    {
        const uintmax_t pct = static_cast<uintmax_t>((current * 100) / total);
        if (pct != previous_pct) {
            previous_pct = pct;
            const int filled = static_cast<int>(static_cast<double>(ProgressBarWidth * pct) / 100.0);
            fmt::print("\r[{:=<{}}{: <{}}] {:3}%", "", filled, "", ProgressBarWidth - filled, pct);
            [[maybe_unused]] int success = std::fflush(stdout);
        }

    }

    inline void ProgressBarFinish()
    {
        fmt::print("\r[{:=<{}}{: <{}}] {:3}%\n", "", ProgressBarWidth, "", 0, 100.0); // 100%
    }

    inline std::uintmax_t ProgressBarInit()
    {
        return 0;
    }

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

        parse_mask_annotations(path);
    }

    void PolygonData::parse_mask_annotations(const std::filesystem::path& path)
    {
        std::ifstream f(path);
        if (!f.is_open()) {
            fmt::println("PolygonData::parse_mask_annotations: could not open {}", path.string());
            return;
        }

        bool found_features = false;

        auto getMaskType = [](const jsoncons::json& feat) -> MaskType
        {
            if (!feat.contains("properties")) return MaskType::None;

            const auto& props = feat.at("properties");

            if (!props.contains("objectType")) return MaskType::None;

            if (props.at("objectType").as<std::string>() == "cell") return MaskType::Cell;

            if (!props.contains("classification")) return MaskType::None;

            const auto& classification = props.at("classification");
            if (!classification.contains("name")) return MaskType::None;

            if (classification.at("name").as<std::string>() == "ROI") return MaskType::Roi;
            if (classification.at("name").as<std::string>() == "TISSUE") return MaskType::Tissue;

            return MaskType::None;
        };

        auto parseName = [](const jsoncons::json& feat, std::vector<std::string>& names, const std::string& prefix, int& counter) -> void
        {
            if (feat.at("properties").contains("name")) {
                names.push_back(feat.at("properties").at("name").as<std::string>());
            }
            else {
                names.push_back(fmt::format("{} {}", prefix, counter++));
            }

        };

        auto parseNameID = [](const jsoncons::json& feat, std::vector<std::string>& names, const std::string& prefix, int& counter) -> void
        {
            if (feat.contains("id")) {
                names.push_back(feat.at("id").as<std::string>());
            }
            else {
                names.push_back(fmt::format("{} {}", prefix, counter++));
            }
        };

        auto parseGeometry = [](const jsoncons::json& feat, std::vector<std::vector<Point2D>>& polygons, const std::string& geometryKey = "geometry") -> void
        {
            std::vector<Point2D>& poly_points = polygons.emplace_back();

            if (feat.contains(geometryKey) && feat.at(geometryKey).contains("coordinates")) {
                const auto& base_coords = feat.at(geometryKey).at("coordinates");
                if (base_coords.empty()) return;

                const auto& first_elem = base_coords.at(0);
                if (first_elem.empty()) return;

                // Default to standard Polygon level
                const jsoncons::json* coordinates = &first_elem;

                // Check an extra level of nesting (this seems occasionally be the case)
                // If first_elem[0][0] is an array, we are nested one level too deep.
                if (first_elem[0].is_array() && !first_elem[0].empty() && first_elem[0][0].is_array()) {
                    coordinates = &first_elem.at(0);
                }

                poly_points.reserve(coordinates->size());
                for (const auto& coords : coordinates->array_range()) {
                    poly_points.push_back(coords.as<Point2D>());
                }
            }
        };

        auto parseGeometryNucleus = [parseGeometry](const jsoncons::json& feat, std::vector<std::vector<Point2D>>& polygons) -> void
        {
            parseGeometry(feat, polygons, "nucleusGeometry");
        };

        auto parseColor = [](const jsoncons::json& feat, std::vector<std::array<uint8_t, 3>>& colors) -> void
        {
            std::array<uint8_t, 3>& color = colors.emplace_back();

            if (feat.at("properties").contains("classification") &&
                feat.at("properties").at("classification").contains("color"))
            {
                const auto feat_color = feat.at("properties").at("classification").at("color").as<std::vector<uint8_t>>();
                color = { feat_color[0], feat_color[1], feat_color[2] };
            }
            else {
                color = { 128, 128, 128 };
            }
        };

        try {
            jsoncons::json_stream_cursor cursor(f);

            int unnamed_roi_counter = 0;
            int unnamed_tissue_counter = 0;
            int unnamed_cell_counter = 0;
            bool in_features_array = false;

            const uintmax_t total_bytes = std::filesystem::file_size(path);
            jsoncons::json_decoder<jsoncons::json> decoder;

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
                    const jsoncons::json feature = decoder.get_result();

                    const auto maskType = getMaskType(feature);

                    if (maskType == MaskType::Roi)
                    {
                        parseName(feature, _names_roi, "ROI", unnamed_roi_counter);
                        parseGeometry(feature, _polygons_roi);
                        parseColor(feature, _colors_roi);
                    }
                    else if (maskType == MaskType::Tissue)
                    {
                        parseNameID(feature, _names_tissue, "TISSUE", unnamed_tissue_counter);
                        parseGeometry(feature, _polygons_tissue);
                        parseColor(feature, _colors_tissue);
                    }
                    else if (maskType == MaskType::Cell)
                    {
                        parseNameID(feature, _names_cell, "CELL", unnamed_cell_counter);
                        parseGeometry(feature, _polygons_cell);
                        parseGeometryNucleus(feature, _polygons_nucleus);
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

        if (!found_features) {
            fmt::println("PolygonData::parse_mask_annotations: json does not contain features field");
            return;
        }

        assert(_names_roi.size() == _colors_roi.size());
        assert(_colors_roi.size() == _polygons_roi.size());
        assert(_polygons_tissue.empty() || _polygons_roi.size() == _polygons_tissue.size());
        assert(_polygons_tissue.empty() || _colors_roi.size() == _polygons_tissue.size());
        assert(_polygons_cell.empty() || _polygons_cell.size() == _polygons_tissue.size());
        assert(_polygons_cell.empty() || _polygons_cell.size() == _polygons_nucleus.size());
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
            std::vector<Point2D> points_scaled(points.size());

#pragma omp parallel for
            for (int64_t i = 0; i < static_cast<int64_t>(points.size()); ++i) {
                points_scaled[i] = {
                    .x = std::round(points[i].x * scaleFactorWidth),
                    .y = std::round(points[i].y * scaleFactorHeight)
                };
            }

            return points_scaled;
            };

        auto last_pct = ProgressBarInit();
        std::uintmax_t currentID = 0;
        for (const auto& coords : polygons) {
            const auto& coords_scaled = (scaleFactorWidth == 1.0) ? coords : scale_coords(coords);
            rasterize_polygon(coords_scaled, imgWidthScaled, imgHeightScaled, indices, pixelCounts);

            ProgressBarPrint(currentID++, last_pct, polygons.size());
        }
        ProgressBarFinish();

        // flip the mask IDs
#pragma omp parallel for
        for (int64_t id = 0; id < static_cast<int64_t>(indices.size()); ++id) {
            const uint32_t v = indices[id];
            const uint32_t row = v / imgWidthScaled;
            const uint32_t col = v % imgWidthScaled;
            indices[id] = (imgHeightScaled - 1 - row) * imgWidthScaled + col;
        }

        assert(indices.size() == std::reduce(pixelCounts.begin(), pixelCounts.end(), 0ull));

        return { indices , pixelCounts };
    }

    void PolygonData::print_info(const size_t max_polygons_to_show ) const
    {
        fmt::print("PolygonData Information");
    	fmt::print("Image Dimensions: {}x{}\n", _img_width, _img_height);
        fmt::print("Total Polygons Detected: {}\n", _names_tissue.size());

        // Print details for a limited number of polygons
        const size_t polygons_to_show = std::min(_names_tissue.size(), max_polygons_to_show);

        fmt::print("\n--- Polygon Details ({}/{} shown) ---\n",
            polygons_to_show, _names_tissue.size());

        for (size_t i = 0; i < polygons_to_show; ++i) {
            fmt::print("  - Polygon {}: Name='{}', Color=({}, {}, {})\n",
                i + 1,
                _names_tissue[i],
                _colors_roi[i][0], _colors_roi[i][1], _colors_roi[i][2]);
        }

        if (_names_tissue.size() > max_polygons_to_show) {
            fmt::print("  ... {} more polygons not shown.\n", _names_tissue.size() - max_polygons_to_show);
        }

        fmt::print("{:-<60}\n", "");
    }


} // namespace PyramidTiffData
