#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Forward declaration from tiffio.h
struct tiff;
typedef struct tiff TIFF; 
typedef uint32_t ttag_t;    // directory tag

// Tag overview: 
// https://exiftool.org/TagNames/EXIF.html
// https://exiftool.sourceforge.net//TagNames/EXIF.html

namespace PyramidTiffData
{
    // =============================================================================
	// Data structs
	// =============================================================================

    struct Image {
        uint32_t width{};           // full-resolution width
        uint32_t height{};          // full-resolution height
        uint32_t channels{};        // total number of channel planes
        std::vector<std::string> channel_names{};
        std::vector<float> data{};
    };

    struct ImageLevelInfo {
        uint32_t width{};
        uint32_t height{};
        uint16_t bits_per_sample{};
        uint16_t sample_format{};   // SAMPLEFORMAT_UINT=1, INT=2, IEEEFP=3

        // One SubIFD offset per channel (same length as TiffSeries::channel_ifds).
        // Empty when the level is itself a top-level IFD (ifd_index >= 0).
        std::vector<uint64_t> channel_subifd_offsets{};
    };

    struct TiffSeries {
        uint32_t width{};           // full-resolution width
        uint32_t height{};          // full-resolution height
        uint32_t channels{};        // total number of channel planes
        uint16_t bits_per_sample{};
        uint16_t sample_format{};

        // IFD indices (0-based) for every channel plane at full resolution.
        std::vector<uint32_t> channel_ifds{};

        std::vector<std::string> channel_names{};

        // Pyramid levels in descending resolution order (index 0 = full res
        // represented by channel_ifds; index 1... = sub-sampled levels stored as
        // SubIFDs).  pyramid[0] mirrors the full-res metadata but its
        // channel_subifd_offsets vector is empty - use channel_ifds instead.
        std::vector<ImageLevelInfo> pyramid{};

        [[nodiscard]] double scaleFactor(const size_t level) const {
           return static_cast<double>(pyramid.at(level).width) / static_cast<double>(width);
        } 
    };

    // Collect metadata from the currently active IFD.
    struct IfdMeta {
        uint32_t w{}, h{};
        uint16_t bps{}, fmt{}, spp{};
        uint32_t subfile_type{};
        std::string page_name{};    // currently not used in this project
        std::string desc{};         // used to store OME description and parse channel names
    };

    // =============================================================================
	// Helper
	// =============================================================================

    // Read a single tag with a fallback default value.
    template<typename T>
    T get_field_default(TIFF* tif, const ttag_t tag, const T default_val) {
        T v = default_val;
        TIFFGetFieldDefaulted(tif, tag, &v);
        return v;
    }

    IfdMeta read_ifd_meta(TIFF* tif);

    // returns -1 if unknown
    std::int32_t check_compression(TIFF* tif);

    // Decode one scanline worth of samples into `dst` as float.
	// `src` points to the raw bytes; `n_samples` is the number of values.
    void decode_scanline_to_float(
        const void* src,
        float* dst,
        size_t   n_samples,
        uint16_t bits_per_sample,
        uint16_t sample_format);

    /**
	 * Writes each channel in the provided level data to a separate TIFF file.
	 * Data is assumed to be in [Channel][Height][Width] (C, H, W) order.
	*/
    void write_to_disk_as_single_page_tiffs(
        const Image& image,
        const std::filesystem::path& output_dir = "./output_channels");

    std::vector<std::string> parse_ome_channel_names(const std::string& ome_xml);

    // =============================================================================
    // OmeTiffPyramid
    // =============================================================================

    /*
     * OmeTiffPyramid pyramid("./some_folder/some_image.ome.tiff");
     * pyramid.printSummary();
     * 
     * std::size_t num_series = pyramid.num_series();   // probably 1
     * std::size_t num_levels = pyramid.num_levels();   // defaults to series 0
     *
     * // Data is flat [channels * height * width], channel-major
     * // Index as: data[c * height * width + row * width + col]
     * const ImageLevel single_level = reader.read_level(0, num_levels - 1);
     *
     */
    class OmeTiffPyramid {
    public:
        OmeTiffPyramid() = default;
        explicit OmeTiffPyramid(const std::filesystem::path& path);
        ~OmeTiffPyramid();
        OmeTiffPyramid(const OmeTiffPyramid&) = delete;
        OmeTiffPyramid& operator=(const OmeTiffPyramid&) = delete;
        OmeTiffPyramid(OmeTiffPyramid&&) = delete;
        OmeTiffPyramid& operator=(OmeTiffPyramid&&) = delete;

        // Lazily load one level: returns float32 data shaped [channels, height, width]
        // Only reads the IFDs for this level - other levels untouched.
        [[nodiscard]] Image read_level(const size_t series_idx, const size_t level_idx) const;
        [[nodiscard]] Image read_level(const size_t level_idx) const { return read_level(0, level_idx); }

        [[nodiscard]] size_t num_series() const noexcept { return _series.size(); }

        [[nodiscard]] size_t num_levels(const size_t series) const noexcept { return _series.at(series).pyramid.size(); }
        [[nodiscard]] size_t num_levels() const noexcept { return _series.at(0).pyramid.size(); }

        [[nodiscard]] const TiffSeries& series(size_t idx = 0) const;

        void print_info() const;

        void init(const std::filesystem::path& path);

    private:
        void open_tiff(const std::filesystem::path& path);
        void close_tiff() const;
        void scan_series();

        // Handle Tiled Images (Crucial for large OME-TIFFs)
        void read_tiled_ifd(float* out, uint32_t w, uint32_t h, uint16_t spp, uint16_t bps, uint16_t fmt) const;
        void read_stripped_ifd(float* out, uint32_t w, uint32_t h, uint16_t spp, uint16_t bps, uint16_t fmt) const;

        // Read all scanlines from the currently-set IFD into `output` starting at
		// byte offset `channel_sample_offset` (in units of float elements).
		// `level_width`, `level_height` and `level_channels` describe the full
		// level (not just this one IFD's plane).
		//
		// `spp` is the samples-per-pixel reported by *this* IFD.  For
		// PLANARCONFIG_CONTIG the IFD holds spp interleaved channels; for
		// PLANARCONFIG_SEPARATE each IFD holds exactly one channel.
        void read_ifd_scanlines(
            float* output,
            uint32_t      level_width,
            uint32_t      level_height,
            uint32_t      channel_offset,   // channel index of first sample in this IFD
            uint16_t      spp,
            uint16_t      bps,
            uint16_t      fmt) const;

        // Core implementation shared by read_series() and read_level().
		// Output layout: float[channel][row][col]  (C-major, row-major)
        std::vector<float> read_level_impl(const size_t series_idx, const size_t level_idx) const;

    private:
        TIFF*                       _tif = nullptr;
        std::vector<TiffSeries>     _series{};
    };

} // namespace PyramidTiffData
