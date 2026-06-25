#include "OmeTiffPyramid.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <regex>
#include <string>
#include <stdexcept>
#include <vector>

#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <tiffio.h> 

namespace PyramidTiffData {

    // =============================================================================
	// Helper
	// =============================================================================

    // Codecs libtiff handles transparently in TIFFReadScanline,
    // provided it was compiled with the relevant support.
    static constexpr std::array<uint16_t, 7> COMPRESSION_KNOWN_CODECS = {
        COMPRESSION_NONE,
        COMPRESSION_LZW,
        COMPRESSION_DEFLATE,        // 32946 - older zlib variant
        COMPRESSION_ADOBE_DEFLATE,  // 8     - standard zlib/deflate
        COMPRESSION_ZSTD,           // 50000
        COMPRESSION_PACKBITS,
        COMPRESSION_JPEG,
    };

    std::int32_t check_compression(TIFF* tif) {
        uint16_t compression = COMPRESSION_NONE;
        TIFFGetFieldDefaulted(tif, TIFFTAG_COMPRESSION, &compression);

        if (std::ranges::find(COMPRESSION_KNOWN_CODECS, compression) == COMPRESSION_KNOWN_CODECS.end()) {
            return -1;
        }

        if (!TIFFIsCODECConfigured(compression)) {
            // Give a specific hint for the two codecs we explicitly support
            std::string hint;
            if (compression == COMPRESSION_DEFLATE ||
                compression == COMPRESSION_ADOBE_DEFLATE) {
                hint = " (libtiff requires zlib; rebuild with zlib support)";
            }
            else if (compression == COMPRESSION_ZSTD) {
                hint = " (libtiff requires libzstd; rebuild with ZSTD support)";
            }
            fmt::print("TIFF compression codec {0} not available in this libtiff build {1}", std::to_string(compression), hint);

        }

        return compression;
    }
    
    IfdMeta read_ifd_meta(TIFF* tif) {
        IfdMeta m{};
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &m.w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &m.h);
        m.bps = get_field_default<uint16_t>(tif, TIFFTAG_BITSPERSAMPLE, 8);
        m.fmt = get_field_default<uint16_t>(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
        m.spp = get_field_default<uint16_t>(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
        TIFFGetFieldDefaulted(tif, TIFFTAG_SUBFILETYPE, &m.subfile_type);
        char* pagename = nullptr;
        if (TIFFGetField(tif, TIFFTAG_PAGENAME, &pagename) && pagename) {
            m.page_name = pagename;
        }
        char* desc = nullptr;
        if (TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &desc) && desc) {
            m.desc = desc;
        }

        return m;
    }

    void decode_scanline_to_float(
        const void* src,
        float* dst,
        size_t   n_samples,
        uint16_t bits_per_sample,
        uint16_t sample_format)
    {
        if (bits_per_sample == 16) {
            if (sample_format == SAMPLEFORMAT_IEEEFP) {
                // 16-bit float (half) - libtiff does not promote these
                // automatically; use a simple bit-cast conversion.
                auto* s = static_cast<const uint16_t*>(src);
                for (size_t i = 0; i < n_samples; ++i) {
                    // IEEE 754 half ? single expansion
                    const uint32_t h = s[i];
                    const uint32_t sgn = (h & 0x8000u) << 16;
                    const uint32_t exp = (h & 0x7C00u) >> 10;
                    const uint32_t man = (h & 0x03FFu);
                    uint32_t f = 0;
                    if (exp == 0)       f = sgn | (man << 13);                      // denorm
                    else if (exp == 31) f = sgn | 0x7F800000u | (man << 13);        // inf/nan
                    else                f = sgn | ((exp + 112) << 23) | (man << 13);
                    std::memcpy(&dst[i], &f, 4);
                }
            }
            else {
                // uint16 (most common for microscopy)
                auto* s = static_cast<const uint16_t*>(src);
                for (size_t i = 0; i < n_samples; ++i)
                    dst[i] = static_cast<float>(s[i]);
            }
        }
        else if (bits_per_sample == 32) {
            if (sample_format == SAMPLEFORMAT_IEEEFP) {
                std::memcpy(dst, src, n_samples * sizeof(float));
            }
            else {
                auto* s = static_cast<const uint32_t*>(src);
                for (size_t i = 0; i < n_samples; ++i)
                    dst[i] = static_cast<float>(s[i]);
            }
        }
        else if (bits_per_sample == 8) {
            auto* s = static_cast<const uint8_t*>(src);
            for (size_t i = 0; i < n_samples; ++i)
                dst[i] = static_cast<float>(s[i]);
        }
        else {
            throw std::runtime_error(
                fmt::format("TiffReader: unsupported bits_per_sample={}", bits_per_sample));
        }
    }

    void write_to_disk_as_single_page_tiffs(const Image& image, const std::filesystem::path& output_dir)
    {
        namespace fs = std::filesystem;
        if (!fs::exists(output_dir)) fs::create_directories(output_dir);

        fmt::println("Exporting {} channels at {}x{}...\n", image.channels, image.width, image.height);

        for (uint32_t c = 0; c < image.channels; ++c) {
            const std::string filename = fmt::format("{:03d}_{}.tif", c, image.channel_names[c]);
            const fs::path file_path = output_dir / filename;

            TIFF* out = TIFFOpen(file_path.string().c_str(), "w");
            if (!out) continue;

            // Set mandatory TIFF tags
            TIFFSetField(out, TIFFTAG_IMAGEWIDTH, image.width);
            TIFFSetField(out, TIFFTAG_IMAGELENGTH, image.height);
            TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 1);
            TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 16);
            TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
            TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
            TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
            TIFFSetField(out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

            // Set compression
            TIFFSetField(out, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);

            // Pointer to the start of this specific channel's float data
            const float* src_plane = image.data.data() + (static_cast<size_t>(image.width) * image.height * c);

            // Buffer to hold one row of converted uint16_t data
            std::vector<uint16_t> row_buffer(image.width);

            for (uint32_t y = 0; y < image.height; ++y) {
                const float* src_row = src_plane + (static_cast<size_t>(y) * image.width);

                // Convert float to uint16_t with clamping
                std::transform(src_row, src_row + image.width, row_buffer.begin(), [](const float val) {
                    return static_cast<uint16_t>(std::clamp(val, 0.0f, 65535.0f));
                    });

                if (TIFFWriteScanline(out, row_buffer.data(), y, 0) < 0) {
                    fmt::println("Error! Failed to write scanline {} for channel {}", y, c);
                    break;
                }
            }

            TIFFClose(out);
        }
        fmt::println("Export complete.");
    }

    // Parses an OME-XML string and extracts the 'Name' attribute from every <Channel> tag.
    std::vector<std::string> parse_ome_channel_names(const std::string& ome_xml) {
        std::vector<std::string> names;

        if (ome_xml.empty()) {
            return names;
        }

        // Regex breakdown:
        // <Channel     : matches the literal start of the tag
        // [^>]*        : matches any characters (like ID, SamplesPerPixel) until a '>'
        // \bName="     : matches the exact attribute Name="
        // ([^"]+)      : Capture Group 1 - matches everything up to the next quote
        std::regex re("<Channel[^>]*\\bName=\"([^\"]+)\"");

        auto begin = std::sregex_iterator(ome_xml.begin(), ome_xml.end(), re);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            names.push_back(it->str(1)); // str(1) is the captured channel name
        }

        return names;
    }

    // =============================================================================
	// OmeTiffPyramid
	// =============================================================================

    OmeTiffPyramid::OmeTiffPyramid(const std::filesystem::path& path)
    {
        init(path);
    }

    OmeTiffPyramid::~OmeTiffPyramid() {
        close_tiff();
    }

    void OmeTiffPyramid::init(const std::filesystem::path& path)
    {
        open_tiff(path);
        scan_series();
    }

    void OmeTiffPyramid::open_tiff(const std::filesystem::path& path) {
        const std::string filePath = path.string();

        _tif = TIFFOpen(filePath.c_str(), "r");

        if (!_tif)
            throw std::runtime_error(fmt::format("OmeTiffPyramid: Cannot open: {}", filePath));

        if (check_compression(_tif) < 0)  // fail early if codec missing
            throw std::runtime_error(fmt::format("OmeTiffPyramid: Cannot interpret: {}", filePath));

    }

    void OmeTiffPyramid::close_tiff() const {
        if (_tif) {
            TIFFClose(_tif);
        }
    }

    void OmeTiffPyramid::scan_series() {
        if (!_tif)
            return;

        TIFFSetDirectory(_tif, 0);

        uint32_t ifd_idx = 0;
        do {
            const auto m = read_ifd_meta(_tif);

            // Skip top-level reduced-image IFDs (some writers put pyramid
            // levels both as SubIFDs *and* as top-level reduced images).
            if (m.subfile_type & FILETYPE_REDUCEDIMAGE) {
                ++ifd_idx;
                continue;
            }

            if (!_series.empty() &&
                _series.back().width == m.w &&
                _series.back().height == m.h)
            {
                // Another channel of the current series
                TiffSeries& s = _series.back();
                s.channel_ifds.push_back(ifd_idx);
                s.channels += m.spp;

                // Append this channel's SubIFD offsets to each pyramid level.
                uint16_t  num_sub = 0;
                uint64_t* sub_offsets_raw = nullptr;
                if (TIFFGetField(_tif, TIFFTAG_SUBIFD, &num_sub, &sub_offsets_raw) &&
                    num_sub > 0 && sub_offsets_raw)
                {
                    // Guard: number of SubIFDs must match what the first
                    // channel registered (skip level 0 which is full-res).
                    std::vector<uint64_t> sub_offsets(sub_offsets_raw, sub_offsets_raw + num_sub);
                	const size_t expect = s.pyramid.size() - 1;
                    for (size_t li = 0; li < expect && li < num_sub; ++li) {
                        s.pyramid[li + 1].channel_subifd_offsets.push_back(
                            sub_offsets[li]);
                    }
                }
            }
            else
            {
                // Level 0 = full resolution (no SubIFD offsets needed here).
                ImageLevelInfo full_res = {
                    .width = m.w,
                    .height = m.h,
                    .bits_per_sample = m.bps,
                    .sample_format = m.fmt,
                    .channel_subifd_offsets = {}
                };

                // New series
                TiffSeries s = {
                    .width = m.w,
                    .height = m.h,
                    .channels = m.spp,
                    .bits_per_sample = m.bps,
                    .sample_format = m.fmt,
                    .channel_ifds = { ifd_idx },
                    .channel_names = parse_ome_channel_names(m.desc),
                    .pyramid = { std::move(full_res) }
                };

                // Discover sub-resolution levels from this channel's SubIFDs.
                uint16_t  num_sub = 0;
                uint64_t* sub_offsets_raw = nullptr;
                if (TIFFGetField(_tif, TIFFTAG_SUBIFD, &num_sub, &sub_offsets_raw) &&
                    num_sub > 0 && sub_offsets_raw)
                {
                    const std::vector<uint64_t> sub_offsets(sub_offsets_raw, sub_offsets_raw + num_sub);

                    for (uint16_t si = 0; si < num_sub; ++si) {
                        if (!TIFFSetSubDirectory(_tif, sub_offsets[si]))
                            continue;

                        const auto lm = read_ifd_meta(_tif);

                        ImageLevelInfo lvl = {
                            .width = lm.w,
                            .height = lm.h,
                            .bits_per_sample = lm.bps,
                            .sample_format = lm.fmt,
                            .channel_subifd_offsets = { sub_offsets[si] }
                        };

                        s.pyramid.push_back(std::move(lvl));
                    }
                    // Return to the IFD we were scanning.
                    TIFFSetDirectory(_tif, ifd_idx);
                }

                _series.push_back(std::move(s));
            }

            ++ifd_idx;
        } while (TIFFReadDirectory(_tif));
    }

    Image OmeTiffPyramid::read_level(const size_t series_idx, const size_t level_idx) const {
        const TiffSeries& s = _series.at(series_idx);
        const ImageLevelInfo& lvl = s.pyramid.at(level_idx);
        const size_t plane_pixels = static_cast<size_t>(lvl.width) * lvl.height;

        Image output = {
            .width = lvl.width,
            .height = lvl.height,
            .channels = 0,          // will be adjusted later
            .channel_names = s.channel_names,
            .data = std::vector<float>(plane_pixels * s.channels, 0.0f)
        };

        try
        {
            uint32_t channel_cursor = 0;
            for (size_t ci = 0; ci < s.channel_ifds.size(); ++ci) {
                // Navigate to Directory
                if (level_idx == 0) {
                    TIFFSetDirectory(_tif, static_cast<tdir_t>(s.channel_ifds[ci]));
                }
                else {
                    TIFFSetSubDirectory(_tif, lvl.channel_subifd_offsets.at(ci));
                }

                const uint16_t spp = get_field_default<uint16_t>(_tif, TIFFTAG_SAMPLESPERPIXEL, 1);
                const uint16_t bps = get_field_default<uint16_t>(_tif, TIFFTAG_BITSPERSAMPLE, 8);
                const uint16_t fmt = get_field_default<uint16_t>(_tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

                float* channel_ptr = output.data.data() + (static_cast<size_t>(channel_cursor) * plane_pixels);

                if (TIFFIsTiled(_tif)) {
                    read_tiled_ifd(channel_ptr, lvl.width, lvl.height, spp, bps, fmt);
                }
                else {
                    read_stripped_ifd(channel_ptr, lvl.width, lvl.height, spp, bps, fmt);
                }
                channel_cursor += spp;
            }

            output.channels = channel_cursor;
        }
        catch (const std::exception& e) {
            fmt::println("OmeTiffPyramid::read_level: Error: {}", e.what());
            output = {};
        }

        return output;
    }

    const TiffSeries& OmeTiffPyramid::series(size_t idx) const {
        if (idx >= _series.size())
            throw std::out_of_range(fmt::format("TiffReader: series index {} out of range ({})",
                idx, _series.size()));
        return _series[idx];
    }

    void OmeTiffPyramid::read_tiled_ifd(float* out, uint32_t w, uint32_t h, uint16_t spp, uint16_t bps, uint16_t fmt) const {
        uint32_t tw, th;
        TIFFGetField(_tif, TIFFTAG_TILEWIDTH, &tw);
        TIFFGetField(_tif, TIFFTAG_TILELENGTH, &th);

        std::vector<uint8_t> tile_buf(TIFFTileSize(_tif));
        std::vector<float> decoded_tile(static_cast<size_t>(tw) * th * spp);

        for (uint32_t y = 0; y < h; y += th) {
            for (uint32_t x = 0; x < w; x += tw) {
                if (TIFFReadTile(_tif, tile_buf.data(), x, y, 0, 0) < 0) continue;

                decode_scanline_to_float(tile_buf.data(), decoded_tile.data(), static_cast<size_t>(tw) * th * spp, bps, fmt);

                // Re-map tile into the global (C, H, W) buffer
                for (uint32_t ty = 0; ty < th && (y + ty) < h; ++ty) {
                    for (uint32_t tx = 0; tx < tw && (x + tx) < w; ++tx) {
                        for (uint16_t c = 0; c < spp; ++c) {
                            // Note: this assumes PlanarConfig Contig inside tile, 
                            // but we map to Planar (C, H, W)
                            const size_t out_idx = static_cast<size_t>(c) * w * h + (y + ty) * w + (x + tx);
                            out[out_idx] = decoded_tile[static_cast<size_t>(ty) * tw * spp + tx * spp + c];
                        }
                    }
                }
            }
        }
    }

    void OmeTiffPyramid::read_stripped_ifd(float* out, uint32_t w, uint32_t h, uint16_t spp, uint16_t bps, uint16_t fmt) const {
        const tsize_t strip_sz = TIFFStripSize(_tif);
        std::vector<uint8_t> buf(strip_sz);
        uint32_t rows_per_strip;
        TIFFGetFieldDefaulted(_tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip);

        for (uint32_t y = 0; y < h; y += rows_per_strip) {
            const uint32_t rows_to_read = std::min(rows_per_strip, h - y);
            if (TIFFReadEncodedStrip(_tif, TIFFComputeStrip(_tif, y, 0), buf.data(), -1) < 0) continue;

            const size_t samples_in_strip = static_cast<size_t>(rows_to_read) * w * spp;
            std::vector<float> decoded(samples_in_strip);
            decode_scanline_to_float(buf.data(), decoded.data(), samples_in_strip, bps, fmt);

            for (uint32_t sy = 0; sy < rows_to_read; ++sy) {
                for (uint32_t sx = 0; sx < w; ++sx) {
                    for (uint16_t c = 0; c < spp; ++c) {
                        const size_t out_idx = static_cast<size_t>(c) * w * h + (y + sy) * w + sx;
                        out[out_idx] = decoded[static_cast<size_t>(sy) * w * spp + static_cast<size_t>(sx) * spp + c];
                    }
                }
            }
        }
    }

    void OmeTiffPyramid::read_ifd_scanlines(
        float*      output,
        uint32_t    level_width,
        uint32_t    level_height,
        uint32_t    channel_offset,   // channel index of first sample in this IFD
        uint16_t    spp,
        uint16_t    bps,
        uint16_t    fmt) const
    {
        tmsize_t scanline_sz = TIFFScanlineSize(_tif);
        std::vector<uint8_t> row_buf(static_cast<size_t>(scanline_sz));

        for (uint32_t y = 0; y < level_height; ++y) {
            if (TIFFReadScanline(_tif, row_buf.data(), y) < 0)
                throw std::runtime_error(
                    fmt::format("TiffReader: TIFFReadScanline failed at row {}", y));

            // Destination: output is channel-major (C, H, W).
            // For interleaved data (spp > 1) we need to de-interleave here.
            for (uint16_t c = 0; c < spp; ++c) {
                float* ch_row = output +
                    static_cast<size_t>(channel_offset + c) * level_height * level_width +
                    static_cast<size_t>(y) * level_width;

                if (spp == 1) {
                    // Common case: one channel per IFD (planar)
                    decode_scanline_to_float(
                        row_buf.data(), ch_row,
                        level_width, bps, fmt);
                }
                else {
                    // Interleaved: stride = spp samples between pixels for channel c
                    // Decode the whole row first, then scatter.
                    std::vector<float> tmp(static_cast<size_t>(level_width) * spp);
                    decode_scanline_to_float(
                        row_buf.data(), tmp.data(),
                        static_cast<size_t>(level_width) * spp, bps, fmt);
                    for (uint32_t x = 0; x < level_width; ++x)
                        ch_row[x] = tmp[x * spp + c];
                }
            }
        }
    }

    std::vector<float> OmeTiffPyramid::read_level_impl(const size_t series_idx, const size_t level_idx) const {
        if (series_idx >= _series.size())
            throw std::out_of_range(
                fmt::format("TiffReader: series index {} out of range", series_idx));

        const TiffSeries& s = _series[series_idx];

        if (level_idx >= s.pyramid.size())
            throw std::out_of_range(
                fmt::format("TiffReader: level index {} out of range ({})",
                    level_idx, s.pyramid.size()));

        const ImageLevelInfo& lvl = s.pyramid[level_idx];

        const size_t total = static_cast<size_t>(lvl.width) *
            static_cast<size_t>(lvl.height) *
            static_cast<size_t>(s.channels);
        std::vector<float> output(total, 0.f);

        uint32_t channel_offset = 0;

        for (size_t ci = 0; ci < s.channel_ifds.size(); ++ci) {
            // Navigate to the right IFD
            if (level_idx == 0) {
                // Full resolution: use the top-level IFD.
                if (!TIFFSetDirectory(_tif, static_cast<tdir_t>(s.channel_ifds[ci])))
                    throw std::runtime_error(
                        fmt::format("TiffReader: TIFFSetDirectory({}) failed",
                            s.channel_ifds[ci]));
            }
            else {
                // Sub-resolution: use the SubIFD offset for this channel.
                if (ci >= lvl.channel_subifd_offsets.size())
                    throw std::runtime_error(
                        fmt::format("TiffReader: no SubIFD offset for channel {} "
                            "at level {}", ci, level_idx));
                if (!TIFFSetSubDirectory(_tif, lvl.channel_subifd_offsets[ci]))
                    throw std::runtime_error(
                        fmt::format("TiffReader: TIFFSetSubDirectory failed "
                            "for channel {}, level {}", ci, level_idx));
            }

            const uint16_t spp = get_field_default<uint16_t>(_tif, TIFFTAG_SAMPLESPERPIXEL, 1);
            const uint16_t bps = get_field_default<uint16_t>(_tif, TIFFTAG_BITSPERSAMPLE, 8);
            const uint16_t fmt = get_field_default<uint16_t>(_tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

            read_ifd_scanlines(
                output.data(),
                lvl.width, lvl.height,
                channel_offset,
                spp, bps, fmt);

            channel_offset += spp;
        }

        return output;
    }

    void OmeTiffPyramid::print_info() const
    {
        fmt::print("OmeTiffPyramid: Found {} series\n", _series.size());
        fmt::print("{:-<60}\n", "");

        for (size_t s_idx = 0; s_idx < _series.size(); ++s_idx) {
            const auto& s = _series[s_idx];

            const char* fmt_str =
                (s.sample_format == SAMPLEFORMAT_IEEEFP) ? "float" :
                (s.sample_format == SAMPLEFORMAT_INT) ? "int" :
                "uint";

            fmt::print("Series {}:\n", s_idx);
            fmt::print("  Dimensions:  {} x {} x {} (Width x Height x Channels)\n",
                s.width, s.height, s.channels);
            fmt::print("  Format:      {}-bit {}\n",
                s.bits_per_sample, fmt_str);
            fmt::print("  Pyramid:     {} levels available\n",
                s.pyramid.size());

            for (size_t l_idx = 0; l_idx < s.pyramid.size(); ++l_idx) {
                const auto& lvl = s.pyramid[l_idx];

                const double scale = s.scaleFactor(l_idx);

                const auto storage =
                    (l_idx == 0)
                    ? fmt::format("{} IFDs", s.channel_ifds.size())
                    : fmt::format("{} SubIFDs", lvl.channel_subifd_offsets.size());

                fmt::print(
                    "    [{}] {:>6} x {:>6} | Scale: {:>5.2f}x | {}\n",
                    l_idx,
                    lvl.width,
                    lvl.height,
                    scale,
                    storage);
            }

            fmt::print("{:-<60}\n", "");
        }
    }


} // namespace PyramidTiffData
