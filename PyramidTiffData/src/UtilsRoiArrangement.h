#pragma once

#include "OmeTiffPyramid.h"
#include "UtilsTransform.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// =============================================================================
// RoiPyramidRepacker
//
// Takes a sparse set of same-size rectangular ROIs scattered over an
// OmeTiffPyramid image and writes a new, smaller pyramid TIFF (same
// series/level/SubIFD layout as OmeTiffPyramid expects, so it can be read
// straight back in with OmeTiffPyramid) in which the ROIs have been packed
// edge-to-edge into a compact near-square grid, in raster-scan order
// (top-to-bottom, then left-to-right, by each ROI's original position).
//
// Example: 5 ROIs scattered like
//   00000
//   01002
//   30040
//   05000
// get relabeled 1..5 in raster order and packed into a ceil(sqrt(5))=3 wide,
// ceil(5/3)=2 tall grid:
//   123
//   450
// (the trailing "0" is background/unused).
//
// The shifted coordinates (one rectangle per ROI, per pyramid level) are
// written out alongside the new TIFF as JSON.
// =============================================================================

namespace PyramidTiffData {

    // One ROI as read from the mask/annotation file: the original polygon ring(s)
    // (kept verbatim so we can translate them later) plus its axis-aligned
    // bounding box, which is what actually drives the packing (ROIs are assumed
    // to be axis-aligned rectangles, per the task description).
    struct Roi {
        std::string id{};
        std::string name{};
        std::vector<Point2D> ring{};                // polygon ring(s), verbatim, full-res coordinates
        double x_min{}, y_min{}, x_max{}, y_max{};

        [[nodiscard]] double width()  const { return x_max - x_min; }
        [[nodiscard]] double height() const { return y_max - y_min; }
    };

    // Where one ROI ends up in the compacted grid.
    struct RoiPlacement {
        Roi roi;
        size_t raster_index{};   // 0-based index in raster-scan order; raster_index+1 is the "1..n" label
        uint32_t grid_row{};
        uint32_t grid_col{};
        // Destination top-left corner at FULL resolution.
        uint32_t dest_x{};
        uint32_t dest_y{};
    };

    // Full layout: uniform cell size + grid shape + every ROI's placement.
    struct RoiLayout {
        uint32_t cell_width{};                  // full-res width of one grid cell (max ROI width, rounded)
        uint32_t cell_height{};                 // full-res height of one grid cell (max ROI height, rounded)
        uint32_t grid_cols{};
        uint32_t grid_rows{};
        uint32_t padding{};                     // full-res gap, in pixels, between adjacent cells
        std::vector<RoiPlacement> placements{}; // in raster_index order

        [[nodiscard]] uint32_t canvas_width()  const {
            return grid_cols * cell_width + (grid_cols > 0 ? (grid_cols - 1) * padding : 0);
        }
        [[nodiscard]] uint32_t canvas_height() const {
            return grid_rows * cell_height + (grid_rows > 0 ? (grid_rows - 1) * padding : 0);
        }
    };

    // A single ROI's rectangle at one pyramid level: where to read it from in the
    // source level, and where/how big to place it in that level's output canvas.
    // src_w/src_h can be smaller than cell_w/cell_h near source image edges after
    // rounding; the destination cell is always cell_w x cell_h (zero-padded).
    struct LevelRoiRect {
        uint32_t src_x{}, src_y{}, src_w{}, src_h{};
        uint32_t dest_x{}, dest_y{};
        uint32_t cell_w{}, cell_h{};
        uint32_t pad_x{}, pad_y{};  // layout.padding, scaled to this level
    };

    // ---------------------------------------------------------------------
    // Mask file loading
    // ---------------------------------------------------------------------

    // Loads ROIs from a JSON file. Accepts several common shapes so it doesn't
    // need to match one exact schema:
    //   - {"rois": [ {"id": "...", "coordinates": [[[x,y], ...]]}, ... ]}
    //   - {"annotations": [ ... ]} / {"masks": [ ... ]} / {"regions": [ ... ]}
    //   - GeoJSON FeatureCollection: {"features": [ {"properties": {...},
    //         "geometry": {"coordinates": [[[x,y], ...]]}}, ... ]}
    //   - A bare top-level array: [ {"coordinates": [...]}, ... ]
    //   - A single ROI object at the top level: {"coordinates": [...]}
    // "coordinates" follows GeoJSON Polygon convention: a list of rings, each
    // ring a list of [x, y] points (first == last point to close the ring).
    // Throws std::runtime_error on a file that doesn't contain anything usable.
    [[nodiscard]] std::vector<Roi> load_rois_from_json(const std::filesystem::path& masks_json_path);

    // ---------------------------------------------------------------------
    // Layout
    // ---------------------------------------------------------------------

    // Sorts ROIs raster-scan (top-to-bottom, then left-to-right, by bbox
    // top-left corner) and packs them into a ceil(sqrt(n))-wide grid.
    // Throws if `rois` is empty.
    [[nodiscard]] RoiLayout compute_roi_layout(const std::vector<Roi>& rois, uint32_t padding = 16);

    // Scales an already-computed full-res layout down to pyramid level `level_idx`
    // using PyramidTiffData::TiffSeries::scaleFactorWidth/Height, clamping source
    // rectangles to the level's bounds (relevant only at the source image edges).
    [[nodiscard]] std::vector<LevelRoiRect> scale_placements_to_level(
        const RoiLayout& layout,
        const PyramidTiffData::TiffSeries& series,
        size_t level_idx);

    // ---------------------------------------------------------------------
    // Output
    // ---------------------------------------------------------------------

    // Writes the shifted ROI coordinates for every pyramid level to JSON.
    void save_shifted_coordinates_json(
        const RoiLayout& layout,
        const PyramidTiffData::TiffSeries& series,
        const std::filesystem::path& out_json_path);

    // Full pipeline: for every pyramid level of `series_idx`, reads the level,
    // copies each ROI's pixels into its new compacted position, and writes the
    // result as a new pyramid TIFF with the same channel/SubIFD structure
    // OmeTiffPyramid expects (so the output can be opened with OmeTiffPyramid
    // again). Also writes the shifted-coordinates JSON.
    //
    // `tile_size` controls tiled output (0 disables tiling and writes strips
    // instead; only sensible for small compacted canvases).
    void repack_rois_to_pyramid(
        const std::filesystem::path& tiff_pyramid_path,
        const std::filesystem::path& masks_json_path,
        const std::filesystem::path& out_tiff_path,
        const std::filesystem::path& out_coords_json_path,
        const size_t series_idx = 0,
        const uint32_t tile_size = 256);

} // namespace PyramidTiffData
