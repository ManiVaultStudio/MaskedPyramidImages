#include "UtilsJson.h"
#include "UtilsFiles.h"
#include "UtilsRoiArrangement.h"

#include <filesystem>
#include <string>
#include <string_view>

#include <fmt/base.h>
#include <fmt/std.h>

#include <CLI/CLI.hpp>

// Reads a tiff file that contains an image pyramid
// and writes each chanel of the hightest level to disk
int main(int argc, char* argv[]) {
    CLI::App app{"Reorder and stack pyramid tiff file", "pyramidstackmerge" };

    const std::string usage_msg = "Usage: " + app.get_name() + " -i <tiff-file>";
    app.usage(usage_msg);

    std::filesystem::path in_img_path = "";
    std::filesystem::path in_roi_list_path = "";
    std::string suffix = "_reordered";
    bool dry_run = false;

    app.add_option("-i,--input", in_img_path, "The input tiff image")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("PATH");
    app.add_option("-o,--order", in_roi_list_path, "User-defined order or ROIs in a txt file")
        ->check(CLI::ExistingFile)
        ->type_name("PATH");
    app.add_option("-s,--suffix", suffix, fmt::format("Suffix for the newly generated files, default: '{}'", suffix));
    app.add_flag("-d,--dry-run", dry_run, "Dry run flag: do not write to disk, just print the new layout and target paths, default: not set");

    CLI11_PARSE(app, argc, argv)

    if (dry_run){
        fmt::println("DRY RUN: Nothing will be written to disk");
    }

    const auto in_json_path     = PyramidTiffData::changeExtension(in_img_path, ".geojson");
    const auto out_tiff_path    = PyramidTiffData::insertSuffixExtension(in_img_path, suffix);
    const auto out_json_path    = PyramidTiffData::insertSuffixExtension(in_json_path, suffix);

    fmt::println("Input tiff file: {}", in_img_path);
    fmt::println("Input JSON file: {}", in_json_path);

    if (!in_roi_list_path.empty())
       fmt::println("Input ROI list file: {}", in_roi_list_path);

    fmt::println("Output tiff: {}", out_tiff_path);
    fmt::println("Output json: {}", out_json_path);

	try {
	    PyramidTiffData::repack_rois_to_pyramid(
	        in_img_path, 
	        in_json_path,
            out_tiff_path, 
            out_json_path,
            in_roi_list_path,
            0,
            256,
            dry_run
            );
    }
    catch (const std::exception& e) {
        fmt::println("Error: {}", e.what());
        return 1;
    }

    return 0;
}
