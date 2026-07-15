#include "PolygonData.h"

#include <algorithm>
#include <array>
#include <cassert>
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
            return j.is_array() && j.size() >= 2;
        }

        static PyramidTiffData::Point2D as(const Json& j) {
            // Use .template as<T>() when inside a template traits class
            return { j[0].template as<uint32_t>(), j[1].template as<uint32_t>() };
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

    void rasterize_polygon(const std::vector<Point2D>& points, const uint32_t img_width, const uint32_t img_height,
        std::vector<uint32_t>& indices, std::vector<uint32_t>& pixel_counts)
    {
        if (points.empty()) return;
        if (points.front() != points.back()) return;

        // Find bounding box to limit search area
        uint32_t minY = points[0].y;
        uint32_t maxY = points[0].y;
        for (const auto& [p_x, p_y] : points) {
            minY = std::min(minY, p_y);
            maxY = std::max(maxY, p_y);
        }

        const uint32_t count_before = static_cast<uint32_t>(indices.size());
        const auto img_width_d = static_cast<double>(img_width);
        const auto max_id = static_cast<uint64_t>(img_width) * img_height;

        // Iterate through each scanline
        for (uint32_t y = minY; y <= maxY; ++y) {
            const double scanline = static_cast<double>(y) + 0.5; // pixel center

            std::vector<uint32_t> nodes;
            size_t j = points.size() - 1;

            // Find intersections of the scanline with polygon edges
            for (size_t i = 0; i < points.size(); ++i) {
                const double yi = static_cast<double>(points[i].y);
                const double yj = static_cast<double>(points[j].y);

                if ((yi <= scanline && yj > scanline) || (yj <= scanline && yi > scanline)) {
                	const double nodeX = static_cast<double>(points[i].x)
                        + (scanline - yi) / (yj - yi)
                        * (static_cast<double>(points[j].x) - static_cast<double>(points[i].x));
                	
                    const double clampedX = std::clamp(nodeX, 0.0, img_width_d - 1.0);
                    nodes.push_back(static_cast<uint32_t>(clampedX));
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
                const auto& coordinates = feat.at(geometryKey).at("coordinates").at(0);
                poly_points.reserve(coordinates.size());
                for (const auto& coords : coordinates.array_range()) {
                    poly_points.push_back(coords.as<Point2D>());
                }
            }
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
            bool in_features_array = false;

            uintmax_t last_pct = -1;
            const uintmax_t total_bytes = std::filesystem::file_size(path);
            jsoncons::json_decoder<jsoncons::json> decoder;

            fmt::println("Reading the json file...");

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
                    else
                    {
                        fmt::println("PolygonData::parse_mask_annotations: json feature does not contain ROI or TISSUE");
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

                // progress bar
                const auto pos = f.tellg();
                const uintmax_t pct = total_bytes > 0
                    ? static_cast<int>((static_cast<std::uintmax_t>(pos) * 100) / total_bytes)
                    : 0;
                if (pos > 0 && pct != last_pct) {
                    last_pct = pct;
                    constexpr uintmax_t bar_width = 40;
                    const int filled = static_cast<int>(static_cast<double>(bar_width * pct) / 100.0);
                    fmt::print("\r[{:=<{}}{: <{}}] {:3}%", "", filled, "", bar_width - filled, pct);
                    [[maybe_unused]] int success = std::fflush(stdout);
                }
            }
            fmt::print("\n"); // move past the progress line
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
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::getMaskRoi(const double scaleFactor) const
    {
        return downscaleMask(scaleFactor, _polygons_roi);
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::getMaskTissue(const double scaleFactor) const
    {
        return downscaleMask(scaleFactor, _polygons_tissue);
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::downscaleMask(const double scaleFactor,
        const std::vector<std::vector<Point2D>>& polygons_roi) const
    {
        std::vector<uint32_t> positive_indices{};
        std::vector<uint32_t> pixel_counts{};

        auto scale_coords = [](const std::vector<Point2D>& points, const double scaleFactor) -> std::vector<Point2D> {
            std::vector<Point2D> points_scaled(points.size());

            for (std::size_t i = 0; i < points.size(); ++i) {
                points_scaled[i] = {
                    .x = static_cast<uint32_t>(static_cast<double>(points[i].x) * scaleFactor),
                    .y = static_cast<uint32_t>(static_cast<double>(points[i].y) * scaleFactor)
                };
            }

            return points_scaled;
            };

        if (scaleFactor == 1.0) {
            for (const auto& coords : polygons_roi) {
                rasterize_polygon(coords, _img_width, _img_height, positive_indices, pixel_counts);
            }
        }
        else
        {
            const uint32_t img_width_scaled     = static_cast<uint32_t>(static_cast<double>(_img_width) * scaleFactor);
            const uint32_t img_height_scaled    = static_cast<uint32_t>(static_cast<double>(_img_height) * scaleFactor);

            for (const auto& coords : polygons_roi) {
                const auto coords_scaled = scale_coords(coords, scaleFactor);
                rasterize_polygon(coords_scaled, img_width_scaled, img_height_scaled, positive_indices, pixel_counts);
            }
        }

        positive_indices.shrink_to_fit();
        pixel_counts.shrink_to_fit();

        assert(positive_indices.size() == std::reduce(pixel_counts.begin(), pixel_counts.end(), 0ull));

        return { positive_indices , pixel_counts };
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
