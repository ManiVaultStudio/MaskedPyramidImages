#include "PolygonData.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <stdio.h>

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

        try {
            jsoncons::json_stream_cursor cursor(f);

            int unnamed_roi_counter = 0;
            int unnamed_tissue_counter = 0;
            bool in_features_array = false;

            uintmax_t last_pct = -1;
            const uintmax_t total_bytes = std::filesystem::file_size(path);
            jsoncons::json_decoder<jsoncons::json> decoder;

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

                    if (!feature.contains("properties") ||
                        !feature.at("properties").contains("classification") ||
                        !feature.at("properties").at("classification").contains("name"))
                        break;

                    const std::string maskType = feature.at("properties").at("classification").at("name").as<std::string>();

                    if (maskType == "ROI")
                    {
                        if (feature.at("properties").contains("name")) {
                            _names_roi.push_back(feature.at("properties").at("name").as<std::string>());
                        }
                        else {
                            _names_roi.push_back(fmt::format("ROI {}", unnamed_roi_counter++));
                        }
                    }
                    else if (maskType == "TISSUE")
                    {
                        if (feature.contains("id")) {
                            _names_tissue.push_back(feature.at("id").as<std::string>());
                        }
                        else {
                            _names_tissue.push_back(fmt::format("TISSUE {}", unnamed_tissue_counter++));
                        }
                    }
                    else
                    {
                        fmt::println("PolygonData::parse_mask_annotations: json feature does not contain ROI or TISSUE");
                        break;
                    }

                    std::vector<Point2D>& poly_points = (maskType == "ROI") 
                		? _polygons_roi.emplace_back()
                		: _polygons_tissue.emplace_back();

                	if (feature.contains("geometry") && feature.at("geometry").contains("coordinates")) {
                        const auto& feat_coordinates = feature.at("geometry").at("coordinates").at(0);
                        poly_points.reserve(feat_coordinates.size());
                        for (const auto& coords : feat_coordinates.array_range()) {
                            poly_points.push_back(coords.as<Point2D>());
                        }
                    }

                    // TODO: handle cell, which contains "geometry" and "nucleusGeometry", 
                    //       but "properties" does not have a "name"

                    if (feature.at("properties").contains("classification") &&
                        feature.at("properties").at("classification").contains("color"))
                    {
                        const auto feat_color = feature.at("properties").at("classification").at("color")
                            .as<std::vector<uint8_t>>();
                        _colors_roi.push_back({ feat_color[0], feat_color[1], feat_color[2] });
                    }
                    else {
                        _colors_roi.push_back({ 128, 128, 128 });
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

                // Progress bar - update only when the percentage actually changes,
				// so we're not flooding stdout on every event.
                const uintmax_t bytes_read = static_cast<std::uintmax_t>(f.tellg());
                const uintmax_t pct = total_bytes > 0
                    ? static_cast<int>((bytes_read * 100) / total_bytes)
                    : 0;
                if (pct != last_pct) {
                    last_pct = pct;
                    constexpr uintmax_t bar_width = 40;
                    const int filled = static_cast<int>(static_cast<double>(bar_width * pct) / 100.0);
                    fmt::print("\r[{:=<{}}{: <{}}] {:3}%",
                        "", filled, "", bar_width - filled, pct);
                    fflush(stdout);
                }
            }
            fmt::print("\n"); // move past the progress line once done
        }
        catch (const std::exception& err) {
            fmt::print("PolygonData::parse_mask_annotations: json parse error: {}", err.what());
            return;
        }

        if (!found_features) {
            fmt::print("PolygonData::parse_mask_annotations: json does not contain features field");
            return;
        }

        assert(_pixel_counts.size() == _names.size());
        assert(_names.size() == _colors.size());
        assert(_colors.size() == _polygons_roi.size());
        assert(_polygons_tissue.empty() || _polygons_roi.size() == _polygons_tissue.size());
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::downscaleMaskRoi(const double scaleFactor) const
    {
        return downscaleMask(scaleFactor, _polygons_roi);
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::downscaleMaskTissue(const double scaleFactor) const
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

        assert(positive_indices_scaled.size() == std::reduce(pixel_counts_scaled.begin(), pixel_counts_scaled.end(), 0ull));

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
