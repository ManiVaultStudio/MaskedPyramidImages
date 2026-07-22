#include "UtilsRoiArrangement.h"

#include "UtilsJson.h"
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

namespace PyramidTiffData {

    // =============================================================================
	// Helper
	// =============================================================================

    namespace {
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
    }

    // =============================================================================
    // Mask JSON loading
    // =============================================================================

    using namespace jsoncons;

    std::tuple<
        std::vector<Roi>, // ROI
        std::vector<Roi>, // TISSUE
        std::vector<Roi>, // CELL
        std::vector<Roi>> // NUCLEUS
	load_rois_from_json(const std::filesystem::path& json_path) {
        std::ifstream input_file(json_path);
        const uintmax_t total_bytes = std::filesystem::file_size(json_path);

        json_stream_cursor cursor(input_file);
        json_decoder<json> decoder;

        std::vector<PyramidTiffData::Roi> rois;
        std::vector<PyramidTiffData::Roi> tissues;
        std::vector<PyramidTiffData::Roi> cells;
        std::vector<PyramidTiffData::Roi> nuclei;

        int unnamed_roi_counter = 0;
        int unnamed_roi_counter_id = 0;
        int unnamed_tissue_counter = 0;
        int unnamed_cell_counter = 0;
        std::string current_roi_name;
        bool in_features_array = false;

        auto assign_min_max = [](PyramidTiffData::Roi& mask)
            {
                double min_x = std::numeric_limits<double>::max();
                double min_y = std::numeric_limits<double>::max();
                double max_x = std::numeric_limits<double>::lowest();
                double max_y = std::numeric_limits<double>::lowest();

                for (const auto& coord : mask.ring)
                {
                    min_x = std::min(min_x, coord.x);
                    min_y = std::min(min_y, coord.y);
                    max_x = std::max(max_x, coord.x);
                    max_y = std::max(max_y, coord.y);
                }

                mask.x_min = min_x; mask.x_max = max_x;
                mask.y_min = min_y; mask.y_max = max_y;
            };


        auto parseMask = [&, assign_min_max](const json& feature, MaskType maskType) -> void
            {
                PyramidTiffData::Roi mask;

                if (maskType == MaskType::Roi) {
                    parseName(feature, mask.name, "ROI", unnamed_roi_counter);
                    parseNameID(feature, mask.id, "ROI", unnamed_roi_counter_id);
                	current_roi_name = mask.name;
                    parseGeometry(feature, mask.ring);
                    assign_min_max(mask);
                    parseColor(feature, mask.color);
                    rois.push_back(std::move(mask));
                }
                else if (maskType == MaskType::Tissue) {
                    parseNameID(feature, mask.id, "TISSUE", unnamed_tissue_counter);
                    mask.name = current_roi_name;
                    parseGeometry(feature, mask.ring);
                    assign_min_max(mask);
                    parseColor(feature, mask.color);
                    tissues.push_back(std::move(mask));
                }
                else if (maskType == MaskType::Cell) {
                    parseNameID(feature, mask.id, "CELL", unnamed_cell_counter);
                    mask.name = current_roi_name;
                    parseGeometry(feature, mask.ring);
                    assign_min_max(mask);

                    PyramidTiffData::Roi maskNucleus;
                    maskNucleus.name = current_roi_name;
                    maskNucleus.id = mask.id;
                    parseGeometryNucleus(feature, maskNucleus.ring);
                    assign_min_max(maskNucleus);

                    cells.push_back(std::move(mask));
                    nuclei.push_back(std::move(maskNucleus));
                }
            };

        // Stream through the input
        auto last_pct = ProgressBarInit();
        for (; !cursor.done(); cursor.next())
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
                    const json feature = decoder.get_result();

                    const auto maskType = getMaskType(feature);
                    parseMask(feature, maskType);
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

            const auto pos = input_file.tellg();
            if (pos > 0) ProgressBarPrint(static_cast<std::uintmax_t>(pos), last_pct, total_bytes);
        }
        ProgressBarFinish();

        input_file.close();

        return { rois, tissues, cells, nuclei };
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
            RoiPlacement& p = layout.placements.emplace_back();
            p.roi = *sorted[i];
            p.raster_index = i;
            p.grid_row = static_cast<uint32_t>(i / layout.grid_cols);
            p.grid_col = static_cast<uint32_t>(i % layout.grid_cols);
            p.dest_x = p.grid_col * (layout.cell_width + layout.padding);
            p.dest_y = p.grid_row * (layout.cell_height + layout.padding);
            p.shift_x = p.roi.x_min - p.dest_x;
            p.shift_y = p.roi.y_min - p.dest_y;
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
        const RoiLayout& layout, const std::vector<Roi>* tissues,
        const std::vector<Roi>* cells, const std::vector<Roi>* nuclei,
        const std::filesystem::path& out_json_path)
    {
        assert(!tissues || tissues->size() == layout.placements.size());
        assert(!cells || cells->size() == nuclei->size());

        std::ofstream output_file(out_json_path);
        json_stream_encoder encoder(output_file);

        encoder.begin_object();
        encoder.key("type");
        encoder.string_value("FeatureCollection");
        encoder.key("features");
        encoder.begin_array();

        auto parse_geometry = [&layout](ojson& feat, const RoiPlacement& placement, const std::vector<Point2D>& ring, const std::string& feat_name = "geometry")
        {
            ojson geometry(jsoncons::json_object_arg);
            geometry["type"] = "Polygon";

            std::vector<PyramidTiffData::Point2D> coords;
            coords.reserve(ring.size());
            for (const auto& pt : ring) {
                const double nx = pt.x - placement.shift_x;
                const double ny = pt.y - placement.shift_y;
                coords.emplace_back(nx, ny);
            }

            // This mirrors the nesting of the original file
            geometry["coordinates"] = std::vector<std::vector<Point2D>>{ coords };
            feat[feat_name] = std::move(geometry);
        };

    	auto parse_feature = [&encoder, parse_geometry](const std::string& maskType, const std::string& maskID, 
            const RoiPlacement& placement, const std::vector<Point2D>& ring_geom, const std::vector<Point2D>* ring_nucleus = nullptr)
	    {
            ojson feat(jsoncons::json_object_arg);

            feat["type"] = "Feature";
            feat["id"] = maskID;

	        parse_geometry(feat, placement, ring_geom);

            if (ring_nucleus)
                parse_geometry(feat, placement, *ring_nucleus, "nucleusGeometry");

            // Properties
			if (maskType == "ROI" || maskType == "TISSUE")
            {
                ojson properties(jsoncons::json_object_arg);
                properties["objectType"] = "annotation";
                properties["name"] = placement.roi.name;

                ojson classification(jsoncons::json_object_arg);

                classification["name"] = maskType;

                ojson color(json_array_arg);
                color.push_back(placement.roi.color[0]);
                color.push_back(placement.roi.color[1]);
                color.push_back(placement.roi.color[2]);
                classification["color"] = std::move(color);

                properties["classification"] = std::move(classification);
                properties["isLocked"] = true;

                feat["properties"] = std::move(properties);
            }
            else
            {
                ojson properties(jsoncons::json_object_arg);
                properties["objectType"] = "cell";
                feat["properties"] = std::move(properties);
            }

            feat.dump(encoder);
	    };

        auto last_pct = ProgressBarInit();
        for (std::size_t roi_counter = 0; roi_counter < layout.placements.size(); ++roi_counter) {
            const auto& p = layout.placements[roi_counter];
            parse_feature("ROI", p.roi.id, p, p.roi.ring);

            if (tissues)
            {
                const auto& tissue = tissues->at(roi_counter);
                parse_feature("TISSUE", tissue.id, p, tissue.ring);
            }

            if (cells && nuclei)
            {
                const std::string& roi_name = p.roi.name;

                for (std::size_t i = 0; i < cells->size(); ++i) {
                    const auto& cell = cells->at(i);
                    const auto& nucleus = nuclei->at(i);
                    if (cell.name != roi_name) continue;

                    parse_feature("CELL", cell.id, p, cell.ring, &(nucleus.ring));
                }

            }

            ProgressBarPrint(roi_counter, last_pct, layout.placements.size());
        }
        ProgressBarFinish();

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
                const uint32_t canvas_w = layout.grid_cols * cell_w + layout.grid_cols * pad_x;
                const uint32_t canvas_h = layout.grid_rows * cell_h + layout.grid_rows * pad_y;

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
        const auto [rois, tissues, cells, nuclei] = load_rois_from_json(masks_json_path);

        fmt::println("Computing new ROIs...");
        const RoiLayout layout = compute_roi_layout(rois);

        fmt::println("RoiArrangement: packing {} ROIs into a {}x{} grid ({}x{} px cells at full res)",
            layout.placements.size(), layout.grid_cols, layout.grid_rows,
            layout.cell_width, layout.cell_height);

        fmt::println("Save new json to {}", out_coords_json_path);
        save_shifted_coordinates_json(layout, &tissues, &cells, &nuclei, out_coords_json_path);

        fmt::println("Shifting ROIs from {}", tiff_pyramid_path);
        const std::vector<LevelCanvas> canvases = shift_roi_to_new_canvas(tiff_pyramid, series, series_idx, layout);

        fmt::println("Writing new pyramid tiff to {}", out_tiff_path);
        write_series_to_pyramid_tiff(out_tiff_path, series, canvases, tile_size);

        fmt::println("Finished.");
    }

} // namespace RoiArrangement
