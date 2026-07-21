#include "UtilsRoiArrangement.h"

#include "UtilsTransform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/encode_json.hpp>
#include <jsoncons/json_cursor.hpp>
#include <jsoncons/json_type.hpp>
#include <tiffio.h> 

namespace jsoncons {
    template<class Json>
    struct json_type_traits<Json, PyramidTiffData::Point2D> {
        using allocator_type = typename Json::allocator_type;

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

        // Converts Point2D to JSON
        static Json to_json(const PyramidTiffData::Point2D& val) {
            Json j(json_array_arg);
            j.push_back(val.x);
            j.push_back(val.y);
            return j;
        }

        // Converts Point2D to JSON using a specific allocator
        static Json to_json(const PyramidTiffData::Point2D& val, const allocator_type& alloc) {
            Json j(json_array_arg, alloc);
            j.push_back(val.x);
            j.push_back(val.y);
            return j;
        }
    };
}

namespace PyramidTiffData {

    // =============================================================================
    // Mask JSON loading
    // =============================================================================

    namespace {

        using json = jsoncons::json;

        bool is_point(const json& p) {
            return p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number();
        }

        std::vector<Point2D> parse_ring(const json& ring_json) {
            std::vector<Point2D> pts;
            if (!ring_json.is_array()) return pts;
            pts.reserve(ring_json.size());
            for (const auto& pt : ring_json.array_range()) {
                if (is_point(pt)) pts.push_back(Point2D{ pt[0].as_double(), pt[1].as_double() });
            }
            return pts;
        }

        // Accepts GeoJSON Polygon convention: [[ [x,y], ... ], ...] (list of rings).
        // Also tolerates a single ring passed without the extra nesting level:
        // [ [x,y], ... ].
        std::vector<std::vector<Point2D>> extract_rings(const json& coords) {
            std::vector<std::vector<Point2D>> rings;
            if (!coords.is_array() || coords.empty()) return rings;

            if (is_point(coords[0])) {
                auto pts = parse_ring(coords);
                if (!pts.empty()) rings.push_back(std::move(pts));
            }
            else {
                for (const auto& ring_json : coords.array_range()) {
                    auto pts = parse_ring(ring_json);
                    if (!pts.empty()) rings.push_back(std::move(pts));
                }
            }
            return rings;
        }

        // NOTE: jsoncons::json's default constructor makes an *empty object*,
        // not null (unlike nlohmann::json). Use json::null() explicitly whenever
        // "no value" needs to be distinguishable via is_null().
        json find_coordinates(const json& entry) {
            if (!entry.is_object()) return json::null();
            if (entry.contains("coordinates")) return entry.at("coordinates");
            if (entry.contains("geometry") && entry.at("geometry").is_object() &&
                entry.at("geometry").contains("coordinates"))
                return entry.at("geometry").at("coordinates");
            return json::null();
        }

        std::string stringify(const json& v) {
            if (v.is_string()) return v.as<std::string>();
            std::ostringstream oss;
            oss << v; // compact JSON via jsoncons' operator<<
            return oss.str();
        }

        std::string find_id(const json& entry, size_t fallback_index) {
            static constexpr const char* kKeys[] = { "id", "label", "name", "roi_id" };
            if (entry.is_object()) {
                for (const char* key : kKeys) {
                    if (entry.contains(key) && !entry.at(key).is_null())
                        return stringify(entry.at(key));
                }
                if (entry.contains("properties") && entry.at("properties").is_object()) {
                    const auto& props = entry.at("properties");
                    for (const char* key : kKeys) {
                        if (props.contains(key) && !props.at(key).is_null())
                            return stringify(props.at(key));
                    }
                }
            }
            return fmt::format("{}", fallback_index);
        }

        std::vector<json> find_entries(const json& root) {
            static constexpr const char* kContainerKeys[] = {
                "rois", "annotations", "masks", "regions", "features", "polygons"
            };
            for (const char* key : kContainerKeys) {
                if (root.is_object() && root.contains(key) && root.at(key).is_array()) {
                    std::vector<json> v;
                    for (const auto& e : root.at(key).array_range()) v.push_back(e);
                    return v;
                }
            }
            if (root.is_array()) {
                std::vector<json> v;
                for (const auto& e : root.array_range()) v.push_back(e);
                return v;
            }
            if (root.is_object() && !find_coordinates(root).is_null()) {
                return { root };
            }
            return {};
        }


    } // namespace

    using namespace jsoncons;

    std::vector<Roi> load_rois_from_json(const std::filesystem::path& json_path) {
        std::ifstream input_file(json_path);

        json_stream_cursor cursor(input_file);
        json_decoder<ojson> decoder;

        std::vector<PyramidTiffData::Roi> recs;
        bool in_features_array = false;

        // Stream through the input
        while (!cursor.done())
        {
            const auto& event = cursor.current();

            switch (event.event_type())
            {
            case staj_event_type::key:
            {
                auto key = event.get<jsoncons::string_view>();
                if (key == "features") {
                    in_features_array = true;
                }
                break;
            }

            case staj_event_type::begin_object:
            {
                if (in_features_array)
                {
                    cursor.read_to(decoder);
                    ojson feature = decoder.get_result();

                    if (!feature.contains("properties")) break;

                    const auto& props = feature["properties"];
                    if (!props.contains("classification")) break;

                    const auto& classification = props["classification"];
                    if (!classification.contains("name")) break;

                    PyramidTiffData::Roi roi;

                    if (classification["name"].as<std::string>() == "ROI" &&
                        feature.contains("geometry") && feature["geometry"].contains("coordinates"))
                    {
                        double min_x = std::numeric_limits<double>::max();
                        double min_y = std::numeric_limits<double>::max();
                        double max_x = std::numeric_limits<double>::lowest();
                        double max_y = std::numeric_limits<double>::lowest();

                        auto test = feature["geometry"]["coordinates"][0];

                        for (const auto& coord : feature["geometry"]["coordinates"][0].as<std::vector<PyramidTiffData::Point2D>>())
                        {
                            min_x = std::min(min_x, coord.x);
                            min_y = std::min(min_y, coord.y);
                            max_x = std::max(max_x, coord.x);
                            max_y = std::max(max_y, coord.y);
                            roi.ring.emplace_back(coord.x, coord.y);
                        }

                        roi.x_min = min_x; roi.x_max = max_x;
                        roi.y_min = min_y; roi.y_max = max_y;
                    }

                    if (feature.contains("id")) {
                        roi.id = feature["id"].as<std::string>();
                    }

                    if (props.contains("name")) {
                        roi.name = props["name"].as<std::string>();
                    }

                    recs.push_back(std::move(roi));

                }
                break;
            }

            case staj_event_type::end_array:
            {
                if (in_features_array) {
                    in_features_array = false;
                }
                break;
            }

            default:
                break;
            }

            cursor.next();
        }

        input_file.close();

        return recs;
    }

    // =============================================================================
    // Layout
    // =============================================================================

    RoiLayout compute_roi_layout(const std::vector<Roi>& rois, uint32_t padding) {
        if (rois.empty())
            throw std::runtime_error("RoiArrangement: compute_roi_layout called with no ROIs");

        // Cell size = the largest ROI bbox (rounded outward), so every ROI fits,
        // even if the source rectangles differ by a pixel or two.
        double max_w = 0.0, max_h = 0.0;
        for (const auto& r : rois) {
            max_w = std::max(max_w, r.width());
            max_h = std::max(max_h, r.height());
        }

        RoiLayout layout;
        layout.cell_width  = static_cast<uint32_t>(std::llround(std::ceil(max_w)));
        layout.cell_height = static_cast<uint32_t>(std::llround(std::ceil(max_h)));
        if (layout.cell_width == 0 || layout.cell_height == 0)
            throw std::runtime_error("RoiArrangement: degenerate (zero-size) ROI cell");

        // Raster-scan order: top-to-bottom, then left-to-right, by bbox top-left.
        std::vector<const Roi*> sorted;
        sorted.reserve(rois.size());
        for (const auto& r : rois) sorted.push_back(&r);
        std::sort(sorted.begin(), sorted.end(), [](const Roi* a, const Roi* b) {
            if (a->y_min != b->y_min) return a->y_min < b->y_min;
            return a->x_min < b->x_min;
        });

        const size_t n = sorted.size();
        layout.grid_cols = std::max<uint32_t>(
            1u, static_cast<uint32_t>(std::llround(std::ceil(std::sqrt(static_cast<double>(n))))));
        layout.grid_rows = static_cast<uint32_t>((n + layout.grid_cols - 1) / layout.grid_cols);
        layout.padding = padding;

        layout.placements.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            RoiPlacement p;
            p.roi = *sorted[i];
            p.raster_index = i;
            p.grid_row = static_cast<uint32_t>(i / layout.grid_cols);
            p.grid_col = static_cast<uint32_t>(i % layout.grid_cols);
            p.dest_x = p.grid_col * (layout.cell_width + padding);
            p.dest_y = p.grid_row * (layout.cell_height + padding);
            layout.placements.push_back(std::move(p));
        }

        return layout;
    }

    std::vector<LevelRoiRect> scale_placements_to_level(
        const RoiLayout& layout,
        const PyramidTiffData::TiffSeries& series,
        size_t level_idx)
    {
        const auto& lvl = series.pyramid.at(level_idx);
        const double sx = series.scaleFactorWidth(level_idx);
        const double sy = series.scaleFactorHeight(level_idx);

        const uint32_t cell_w_L = std::max<uint32_t>(
            1, static_cast<uint32_t>(std::llround(layout.cell_width * sx)));
        const uint32_t cell_h_L = std::max<uint32_t>(
            1, static_cast<uint32_t>(std::llround(layout.cell_height * sy)));
        const uint32_t pad_x_L = static_cast<uint32_t>(std::llround(layout.padding * sx));
        const uint32_t pad_y_L = static_cast<uint32_t>(std::llround(layout.padding * sy));

        std::vector<LevelRoiRect> rects;
        rects.reserve(layout.placements.size());

        for (const auto& p : layout.placements) {
            LevelRoiRect r;
            r.cell_w = cell_w_L;
            r.cell_h = cell_h_L;
            r.pad_x = pad_x_L;
            r.pad_y = pad_y_L;
            r.dest_x = p.grid_col * (cell_w_L + pad_x_L);
            r.dest_y = p.grid_row * (cell_h_L + pad_y_L);

            int64_t src_x = std::llround(p.roi.x_min * sx);
            int64_t src_y = std::llround(p.roi.y_min * sy);
            src_x = std::clamp<int64_t>(src_x, 0, static_cast<int64_t>(lvl.width));
            src_y = std::clamp<int64_t>(src_y, 0, static_cast<int64_t>(lvl.height));

            uint32_t src_w = cell_w_L;
            uint32_t src_h = cell_h_L;
            if (static_cast<uint64_t>(src_x) + src_w > lvl.width)
                src_w = static_cast<uint32_t>(lvl.width - static_cast<uint32_t>(src_x));
            if (static_cast<uint64_t>(src_y) + src_h > lvl.height)
                src_h = static_cast<uint32_t>(lvl.height - static_cast<uint32_t>(src_y));

            r.src_x = static_cast<uint32_t>(src_x);
            r.src_y = static_cast<uint32_t>(src_y);
            r.src_w = src_w;
            r.src_h = src_h;

            rects.push_back(r);
        }
        return rects;
    }

    // =============================================================================
    // Shifted-coordinates JSON
    // =============================================================================

    void save_shifted_coordinates_json(
        const RoiLayout& layout,
        const PyramidTiffData::TiffSeries& series,
        const std::filesystem::path& out_json_path)
    {
        std::ofstream output_file(out_json_path);
        json_stream_encoder encoder(output_file);

        encoder.begin_object();
        encoder.key("type");
        encoder.string_value("FeatureCollection");
        encoder.key("features");
        encoder.begin_array();

        for (const auto& p : layout.placements) {
            ojson feature(jsoncons::json_object_arg);
            feature["type"] = "Feature";
            feature["id"] = p.roi.id;

            // Geometry
	        {
		        ojson geometry(jsoncons::json_object_arg);
            	geometry["type"] = "Polygon";

            	const double sx = series.scaleFactorWidth(0);
            	const double sy = series.scaleFactorHeight(0);
            	const uint32_t cell_w_L = std::max<uint32_t>(
					1, static_cast<uint32_t>(std::llround(layout.cell_width * sx)));
            	const uint32_t cell_h_L = std::max<uint32_t>(
					1, static_cast<uint32_t>(std::llround(layout.cell_height * sy)));
            	const double dest_x_L = static_cast<double>(p.grid_col) * cell_w_L;
            	const double dest_y_L = static_cast<double>(p.grid_row) * cell_h_L;
        	
            	std::vector<PyramidTiffData::Point2D> coords;
            	coords.reserve(p.roi.ring.size());
            	for (const auto& pt : p.roi.ring) {
            		const double nx = (pt.x - p.roi.x_min) * sx + dest_x_L;
            		const double ny = (pt.y - p.roi.y_min) * sy + dest_y_L;
            		coords.emplace_back(nx, ny);
            	}

                // This mirrors the nesting of the original file
            	geometry["coordinates"] = std::vector<std::vector<Point2D>>{ coords };
            	feature["geometry"] = std::move(geometry);
	        }

            // Properties
            {
                ojson properties(jsoncons::json_object_arg);
                properties["objectType"] = "annotation";
                properties["name"] = p.roi.name;

                ojson classification(jsoncons::json_object_arg);
                classification["name"] = "ROI";

                ojson color(json_array_arg);
                color.push_back(0);
                color.push_back(0);
                color.push_back(255);
                classification["color"] = std::move(color);

                properties["classification"] = std::move(classification);
                properties["isLocked"] = true;

                feature["properties"] = std::move(properties);
            }

            feature.dump(encoder);
        }

        encoder.end_array();
        encoder.end_object();
        encoder.flush();

        output_file.close();
    }

    // =============================================================================
    // Pyramid TIFF writing
    // =============================================================================

    namespace {

        void encode_float_row(const float* src, void* dst, size_t n_samples, uint16_t bps, uint16_t fmt) {
            if (bps == 8) {
                if (fmt == SAMPLEFORMAT_INT) {
                    auto* d = static_cast<int8_t*>(dst);
                    for (size_t i = 0; i < n_samples; ++i)
                        d[i] = static_cast<int8_t>(std::clamp(src[i], -128.f, 127.f));
                } else {
                    auto* d = static_cast<uint8_t*>(dst);
                    for (size_t i = 0; i < n_samples; ++i)
                        d[i] = static_cast<uint8_t>(std::clamp(src[i], 0.f, 255.f));
                }
            } else if (bps == 16) {
                if (fmt == SAMPLEFORMAT_IEEEFP) {
                    throw std::runtime_error("RoiArrangement: writing 16-bit float samples is not supported");
                } else if (fmt == SAMPLEFORMAT_INT) {
                    auto* d = static_cast<int16_t*>(dst);
                    for (size_t i = 0; i < n_samples; ++i)
                        d[i] = static_cast<int16_t>(std::clamp(src[i], -32768.f, 32767.f));
                } else {
                    auto* d = static_cast<uint16_t*>(dst);
                    for (size_t i = 0; i < n_samples; ++i)
                        d[i] = static_cast<uint16_t>(std::clamp(src[i], 0.f, 65535.f));
                }
            } else if (bps == 32) {
                if (fmt == SAMPLEFORMAT_IEEEFP) {
                    std::memcpy(dst, src, n_samples * sizeof(float));
                } else if (fmt == SAMPLEFORMAT_INT) {
                    auto* d = static_cast<int32_t*>(dst);
                    for (size_t i = 0; i < n_samples; ++i)
                        d[i] = static_cast<int32_t>(std::clamp<double>(src[i], -2147483648.0, 2147483647.0));
                } else {
                    auto* d = static_cast<uint32_t*>(dst);
                    for (size_t i = 0; i < n_samples; ++i)
                        d[i] = static_cast<uint32_t>(std::clamp<double>(src[i], 0.0, 4294967295.0));
                }
            } else {
                throw std::runtime_error(fmt::format("RoiArrangement: unsupported bits_per_sample={} for writing", bps));
            }
        }

        // Minimal OME-XML - just enough for PyramidTiffData::parse_ome_channel_names
        // to recover channel names when the output is read back with OmeTiffPyramid.
        // Not a fully schema-valid OME document.
        std::string build_minimal_ome_xml(
            const std::vector<std::string>& channel_names,
            uint32_t n_channels, uint32_t width, uint32_t height)
        {
            std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
            xml += "<OME xmlns=\"http://www.openmicroscopy.org/Schemas/OME/2016-06\">";
            xml += fmt::format(
                "<Image ID=\"Image:0\"><Pixels ID=\"Pixels:0\" SizeX=\"{}\" SizeY=\"{}\" SizeC=\"{}\" "
                "SizeZ=\"1\" SizeT=\"1\" DimensionOrder=\"XYCZT\">",
                width, height, n_channels);
            for (uint32_t c = 0; c < n_channels; ++c) {
                const std::string name = (c < channel_names.size() && !channel_names[c].empty())
                    ? channel_names[c] : fmt::format("Channel {}", c);
                xml += fmt::format("<Channel ID=\"Channel:0:{}\" Name=\"{}\" SamplesPerPixel=\"1\"/>", c, name);
            }
            xml += "</Pixels></Image></OME>";
            return xml;
        }

        // Writes one IFD (main or sub) for a single channel plane and finalizes it
        // with TIFFWriteDirectory. Caller must have already set
        // TIFFTAG_SUBFILETYPE (and, for a level-0 main IFD, TIFFTAG_SUBIFD) before
        // calling this.
        void write_channel_plane(
            TIFF* out,
            const float* plane,
            uint32_t canvas_w, uint32_t canvas_h,
            uint16_t bps, uint16_t sample_fmt,
            uint32_t tile_size,
            const std::string* image_description)
        {
            TIFFSetField(out, TIFFTAG_IMAGEWIDTH, canvas_w);
            TIFFSetField(out, TIFFTAG_IMAGELENGTH, canvas_h);
            TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 1);
            TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, bps);
            TIFFSetField(out, TIFFTAG_SAMPLEFORMAT, sample_fmt);
            TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
            TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
            TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
            TIFFSetField(out, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);
            if (image_description) {
                TIFFSetField(out, TIFFTAG_IMAGEDESCRIPTION, image_description->c_str());
            }

            const bool tiled = tile_size > 0 && canvas_w >= tile_size && canvas_h >= tile_size;
            const size_t bytes_per_sample = bps / 8;

            if (tiled) {
                // Tile dimensions must be multiples of 16 for libtiff.
                const uint32_t tw = std::max<uint32_t>(16, (tile_size / 16) * 16);
                TIFFSetField(out, TIFFTAG_TILEWIDTH, tw);
                TIFFSetField(out, TIFFTAG_TILELENGTH, tw);

                std::vector<uint8_t> tile_buf(static_cast<size_t>(tw) * tw * bytes_per_sample);
                std::vector<float> tile_f(static_cast<size_t>(tw) * tw);

                for (uint32_t ty = 0; ty < canvas_h; ty += tw) {
                    for (uint32_t tx = 0; tx < canvas_w; tx += tw) {
                        std::fill(tile_f.begin(), tile_f.end(), 0.0f);
                        const uint32_t copy_h = std::min(tw, canvas_h - ty);
                        const uint32_t copy_w = std::min(tw, canvas_w - tx);
                        for (uint32_t y = 0; y < copy_h; ++y) {
                            const float* src_row = plane + static_cast<size_t>(ty + y) * canvas_w + tx;
                            float* dst_row = tile_f.data() + static_cast<size_t>(y) * tw;
                            std::copy(src_row, src_row + copy_w, dst_row);
                        }
                        encode_float_row(tile_f.data(), tile_buf.data(),
                            static_cast<size_t>(tw) * tw, bps, sample_fmt);
                        if (TIFFWriteTile(out, tile_buf.data(), tx, ty, 0, 0) < 0)
                            throw std::runtime_error("RoiArrangement: TIFFWriteTile failed");
                    }
                }
            } else {
                TIFFUnsetField(out, TIFFTAG_TILEWIDTH);
                TIFFUnsetField(out, TIFFTAG_TILELENGTH);
                std::vector<uint8_t> row_buf(static_cast<size_t>(canvas_w) * bytes_per_sample);
                for (uint32_t y = 0; y < canvas_h; ++y) {
                    encode_float_row(plane + static_cast<size_t>(y) * canvas_w, row_buf.data(),
                        static_cast<size_t>(canvas_w), bps, sample_fmt);
                    if (TIFFWriteScanline(out, row_buf.data(), y) < 0)
                        throw std::runtime_error("RoiArrangement: TIFFWriteScanline failed");
                }
            }

            if (TIFFWriteDirectory(out) == 0)
                throw std::runtime_error("RoiArrangement: TIFFWriteDirectory failed");
        }

        struct LevelCanvas {
            uint32_t width{}, height{};
            std::vector<std::vector<float>> channel_planes; // [channel][h*w]
        };

        // Compacted canvases are, by construction, much smaller than the source
		// levels, so holding every level's canvas in memory at once is cheap -
		// and it's required, because the TIFF SubIFD chain for one channel must
		// be written as a contiguous run of directories (level 0 then its
		// SubIFDs), so all of a channel's levels must be ready before writing
		// starts for that channel.
        std::vector<LevelCanvas> shift_roi_to_new_canvas(
            const OmeTiffPyramid& tiff_pyramid, 
            const TiffSeries& series, const size_t series_idx,
            const RoiLayout& layout)
        {
            std::vector<LevelCanvas> canvases(series.pyramid.size());

            for (size_t level_idx = 0; level_idx < series.pyramid.size(); ++level_idx) {
                const auto rects = scale_placements_to_level(layout, series, level_idx);
                const uint32_t cell_w = rects.front().cell_w;
                const uint32_t cell_h = rects.front().cell_h;
                const uint32_t pad_x = rects.front().pad_x;
                const uint32_t pad_y = rects.front().pad_y;
                const uint32_t canvas_w = layout.grid_cols * cell_w + (layout.grid_cols > 0 ? (layout.grid_cols - 1) * pad_x : 0);
                const uint32_t canvas_h = layout.grid_rows * cell_h + (layout.grid_rows > 0 ? (layout.grid_rows - 1) * pad_y : 0);

                LevelCanvas& lc = canvases[level_idx];
                lc.width = canvas_w;
                lc.height = canvas_h;
                lc.channel_planes.assign(
                    series.channels, std::vector<float>(static_cast<size_t>(canvas_w) * canvas_h, 0.0f));

                const PyramidTiffData::Image src_level = tiff_pyramid.read_level(series_idx, level_idx);
                if (src_level.channels != series.channels)
                    throw std::runtime_error("RoiArrangement: unexpected channel count reading source level");

                fmt::println("Level {} -> {}x{} (source level {}x{})",
                    level_idx, canvas_w, canvas_h, src_level.width, src_level.height);

                size_t count = 0;
                for (const auto& r : rects) {
                    if (r.src_w == 0 || r.src_h == 0) continue;

                    fmt::println("Rec {}: from {}x{} to {}x{} - size {}x{}", count++, r.src_x, r.src_y, r.dest_x, r.dest_y, r.src_w, r.src_h);

                    // data is stored in [Channel][Height][Width] (C, H, W) order
                    // const size_t out_idx = static_cast<size_t>(c) * w * h + static_cast<size_t>(y + sy) * w + sx;
                    for (uint32_t c = 0; c < series.channels; ++c) {
                        const float* src_plane = src_level.data.data() +
                            static_cast<size_t>(c) * src_level.width * src_level.height;
                        float* dst_plane = lc.channel_planes[c].data();
                        for (uint32_t y = 0; y < r.src_h; ++y) {
                            const float* src_row = src_plane +
                                static_cast<size_t>(r.src_y + y) * src_level.width + r.src_x;
                            float* dst_row = dst_plane +
                                static_cast<size_t>(r.dest_y + y) * canvas_w + r.dest_x;
                            std::copy(src_row, src_row + r.src_w, dst_row);
                        }
                    }
                }
            }

            return canvases;
        }

        void write_series_to_pyramid_tiff(
            const std::filesystem::path& out_path,
            const TiffSeries& series,
            const std::vector<LevelCanvas>& canvases,
            uint32_t tile_size)
        {
            if (series.channels == 0)
                throw std::runtime_error("RoiArrangement: series has no channels");
            if (canvases.empty())
                throw std::runtime_error("RoiArrangement: no pyramid levels to write");
            if (canvases.size() != series.pyramid.size())
                throw std::runtime_error("RoiArrangement: canvas count does not match pyramid level count");
            for (const auto& lc : canvases) {
                if (lc.channel_planes.size() != series.channels)
                    throw std::runtime_error("RoiArrangement: canvas channel count does not match series channel count");
            }

            if (canvases.empty())
                throw std::runtime_error("RoiArrangement: canvases are empty");

            TIFFSetErrorHandler([](const char* module, const char* fmt, va_list ap) {
                fmt::print(stderr, "libtiff error [{}]: ", module ? module : "?");
                vfprintf(stderr, fmt, ap);
                fmt::print(stderr, "\n");
                });

            // "w8" so BigTIFF is used automatically once the file grows past 4GB -
            // large multi-channel pyramids can exceed that even after compaction.
            TIFF* out = TIFFOpen(out_path.string().c_str(), "w8");
            if (!out)
                throw std::runtime_error(fmt::format("RoiArrangement: failed to open {} for writing", out_path.string()));

            // Only attached to IFD 0 (first channel, level 0) - this mirrors what a
            // real OME writer does and is exactly what OmeTiffPyramid::scan_series
            // reads back via parse_ome_channel_names(m.desc) for the new series.
            const std::string ome_xml = build_minimal_ome_xml(
                series.channel_names, series.channels, canvases[0].width, canvases[0].height);

            const uint16_t num_sub_levels = static_cast<uint16_t>(canvases.size() - 1);

            try {
                for (uint32_t c = 0; c < series.channels; ++c) {
                    if (TIFFCreateDirectory(out) != 0)
                        throw std::runtime_error("RoiArrangement: TIFFCreateDirectory failed");

                    // --- Level 0 (full resolution): one top-level IFD per channel ---
                    const LevelCanvas& lvl0 = canvases[0];
                    const ImageLevelInfo& lvl0_info = series.pyramid[0];

                    TIFFSetField(out, TIFFTAG_SUBFILETYPE, 0);

                    std::vector<uint64_t> subifd_placeholder; // must outlive this TIFFSetField call, no longer
                    if (num_sub_levels > 0) {
                        // Reserve `num_sub_levels` SubIFD slots on this channel's main
                        // IFD; libtiff fills in the offsets as we write each one below.
                        subifd_placeholder.assign(num_sub_levels, 0);
                        TIFFSetField(out, TIFFTAG_SUBIFD, num_sub_levels, subifd_placeholder.data());
                    }

                    write_channel_plane(
                        out, lvl0.channel_planes[c].data(),
                        lvl0.width, lvl0.height,
                        lvl0_info.bits_per_sample, lvl0_info.sample_format,
                        tile_size,
                        c == 0 ? &ome_xml : nullptr);

                    // --- Sub-resolution levels, nested as this channel's SubIFD chain ---
                    for (size_t level_idx = 1; level_idx < canvases.size(); ++level_idx) {
                        const LevelCanvas& lc = canvases[level_idx];
                        const ImageLevelInfo& info = series.pyramid[level_idx];

                        if (TIFFCreateDirectory(out) != 0)
                            throw std::runtime_error("RoiArrangement: TIFFCreateDirectory failed");

                        TIFFSetField(out, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);

                        write_channel_plane(
                            out, lc.channel_planes[c].data(),
                            lc.width, lc.height,
                            info.bits_per_sample, info.sample_format,
                            tile_size,
                            nullptr);
                    }
                    // After the last SubIFD is written, libtiff automatically returns
                    // to the top-level directory chain, so the next channel's main
                    // IFD (or EOF) follows naturally.
                }
            }
            catch (...) {
                TIFFClose(out);
                throw;
            }

            TIFFClose(out);
        }

    } // namespace
    
    void repack_rois_to_pyramid(
        const std::filesystem::path& tiff_pyramid_path,
        const std::filesystem::path& masks_json_path,
        const std::filesystem::path& out_tiff_path,
        const std::filesystem::path& out_coords_json_path,
        const size_t series_idx,
        const uint32_t tile_size)
    {
        const OmeTiffPyramid tiff_pyramid = PyramidTiffData::OmeTiffPyramid(tiff_pyramid_path);

        const TiffSeries& series = tiff_pyramid.series(series_idx);
        if (series.pyramid.empty())
        {
            fmt::println("repack_rois_to_pyramid: source series has no pyramid levels");
            return;
        }

        fmt::println("Loading ROIs from {}", masks_json_path);
        const std::vector<Roi> rois = load_rois_from_json(masks_json_path);

        fmt::println("Computing new ROIs...");
        const RoiLayout layout = compute_roi_layout(rois);

        fmt::println("RoiArrangement: packing {} ROIs into a {}x{} grid ({}x{} px cells at full res)",
            layout.placements.size(), layout.grid_cols, layout.grid_rows,
            layout.cell_width, layout.cell_height);

        fmt::println("Save new json to {}", out_coords_json_path);
        save_shifted_coordinates_json(layout, series, out_coords_json_path);

        fmt::println("Shifting ROIs from {}", tiff_pyramid_path);
        const std::vector<LevelCanvas> canvases = shift_roi_to_new_canvas(tiff_pyramid, series, series_idx, layout);

        fmt::println("Writing new pyramid tiff to {}", out_tiff_path);
        write_series_to_pyramid_tiff(out_tiff_path, series, canvases, tile_size);

        fmt::println("Finished.");
    }

} // namespace RoiArrangement
