#include "PolygonData.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#ifndef NDEBUG
#include <numeric>
#endif

#include <fmt/base.h>
#include <fmt/std.h>

#include <nlohmann/json.hpp>

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
        nlohmann::json data;
		try {
            data = nlohmann::json::parse(f);
		}
		catch (const nlohmann::detail::parse_error& err) {
		   fmt::print("PyramidImageData::scan: json parse error: {}", err.what());
		   return;
		}

        if (!data.contains("features")) {
            fmt::print("PyramidImageData::scan: json does not contain features field");
            return;
        }

        int unnamedChannelCounter = 0;
    	for (const auto& feature : data["features"]) {
            
    		if (feature.contains("properties") && feature["properties"].contains("name")) {
    			_names.push_back(feature["properties"]["name"].get<std::string>());
    		}
            else {
                _names.push_back(fmt::format("Channel {}", unnamedChannelCounter++));
            }

            if (feature.contains("properties") &&
                feature["properties"].contains("classification") &&
                feature["properties"]["classification"].contains("color"))
            {
                const auto feat_color = feature["properties"]["classification"]["color"].get<std::vector<int>>();
                _colors.push_back({
                    static_cast<uint8_t>(feat_color[0]),
                    static_cast<uint8_t>(feat_color[1]),
                    static_cast<uint8_t>(feat_color[2])
                    });
            }
            else {
                _colors.push_back({ 128, 128, 128 });
            }
        
            if (feature.contains("geometry") && feature["geometry"].contains("coordinates")) {
                std::vector<Point2D>& poly_points = _polygons.emplace_back();
                const auto feat_coordinates = feature["geometry"]["coordinates"][0];
                poly_points.reserve(feat_coordinates.size());
                for (const auto& coords : feat_coordinates) {
                    poly_points.push_back({ .x = coords[0].get<uint32_t>(), .y = coords[1].get<uint32_t>() });
                }
                rasterize_polygon(poly_points, _img_width, _img_height, _all_positive_indices, _pixel_counts);

            }
        }

        assert(pixel_counts_.size() == names_.size());
        assert(names_.size() == colors_.size());
        assert(colors_.size() == polygons_.size());
    }

    std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> PolygonData::downscaleMask(const double scaleFactor) const
    {
        if (scaleFactor == 1.0) {
            return { _all_positive_indices , _pixel_counts };
        }

        std::vector<uint32_t> positive_indices_scaled;
        std::vector<uint32_t> pixel_counts_scaled;

        positive_indices_scaled.reserve(static_cast<size_t>(static_cast<double>(_all_positive_indices.size()) * scaleFactor));
        pixel_counts_scaled.reserve(_polygons.size());
        
    	const uint32_t img_width_scaled = static_cast<uint32_t>(static_cast<double>(_img_width) * scaleFactor);
        const uint32_t img_height_scaled = static_cast<uint32_t>(static_cast<double>(_img_height) * scaleFactor);

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

        for (const auto& coords : _polygons) {
            const auto coords_scaled = scale_coords(coords, scaleFactor);
            rasterize_polygon(coords_scaled, img_width_scaled, img_height_scaled, positive_indices_scaled, pixel_counts_scaled);
        }

        positive_indices_scaled.shrink_to_fit();

        assert(positive_indices_scaled.size() == std::reduce(pixel_counts_scaled.begin(), pixel_counts_scaled.end(), 0ull));

        return { positive_indices_scaled , pixel_counts_scaled };
    }

    void PolygonData::print_info(const size_t max_polygons_to_show ) const
    {
        fmt::print("PolygonData Information");
    	fmt::print("Image Dimensions: {}x{}\n", _img_width, _img_height);
        fmt::print("Total Polygons Detected: {}\n", _names.size());
        fmt::print("Total Positive Pixels (across all polygons): {}\n", _all_positive_indices.size());

        // Print details for a limited number of polygons
        const size_t polygons_to_show = std::min(_names.size(), max_polygons_to_show);

        fmt::print("\n--- Polygon Details ({}/{} shown) ---\n",
            polygons_to_show, _names.size());

        for (size_t i = 0; i < polygons_to_show; ++i) {
            fmt::print("  - Polygon {}: Name='{}', Pixels={}, Color=({}, {}, {})\n",
                i + 1,
                _names[i],
                _pixel_counts[i],
                _colors[i][0], _colors[i][1], _colors[i][2]);
        }

        if (_names.size() > max_polygons_to_show) {
            fmt::print("  ... {} more polygons not shown.\n", _names.size() - max_polygons_to_show);
        }

        fmt::print("{:-<60}\n", "");
    }


} // namespace PyramidTiffData
