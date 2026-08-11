#include "UtilsJson.h"
#include "UtilsRoiArrangement.h"

#include <filesystem>
#include <string>
#include <string_view>

#include <fmt/base.h>
#include <fmt/std.h>

#include <CLI/CLI.hpp>

namespace utils
{
    static std::filesystem::path changeExtension(
        const std::filesystem::path& p,
        const std::string_view ext)
    {
        std::string filename = p.filename().string();

        // Find the first dot (skipping index 0 to handle hidden files like .gitignore)
        size_t first_dot = filename.find('.', 1);

        if (first_dot == std::string::npos) {
            // If there's no extension at all, use standard behavior
            auto result = p;
            result.replace_extension(ext);
            return result;
        }

        // Extract the stem (everything before the first dot)
        std::string stem = filename.substr(0, first_dot);

        // Prepare the new extension: ensure it starts with a dot if not empty
        std::string formatted_ext(ext);
        if (!formatted_ext.empty() && formatted_ext[0] != '.') {
            formatted_ext.insert(0, ".");
        }

        // Reconstruct the path: Parent Dir + Stem + New Extension
        return p.parent_path() / (stem + formatted_ext);
    }

    static std::filesystem::path insertSuffixExtension(
        const std::filesystem::path& p,
        const std::string& suffix)
    {
        std::string filename = p.filename().string();

        // Find the first dot. 
        // We start searching at index 1 to avoid treating hidden files 
        // (e.g., .gitignore) as having an extension at the start.
        size_t first_dot = filename.find('.', 1);

        if (first_dot == std::string::npos) {
            // No extension found, just append to the end
            return p.parent_path() / (filename + suffix);
        }

        std::string stem = filename.substr(0, first_dot);
        std::string extension = filename.substr(first_dot);

        return p.parent_path() / (stem + suffix + extension);
    }
}

// Reads a tiff file that contains an image pyramid
// and writes each chanel of the hightest level to disk
int main(int argc, char* argv[]) {
    CLI::App app{"Reorder and stack pyramid tiff file", "pyramidstackmerge" };

    const std::string usage_msg = "Usage: " + app.get_name() + " -i <tiff-file>";
    app.usage(usage_msg);

    std::filesystem::path in_img_path = "";
    std::filesystem::path in_roi_list_path = "";
    std::string suffix = "_reordered";

    app.add_option("-i,--input", in_img_path, "The input tiff image")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("PATH");
    app.add_option("-o,--order", in_roi_list_path, "User defined order or ROIs")
        ->check(CLI::ExistingFile)
        ->type_name("PATH");
    app.add_option("-s,--suffix", suffix, "Suffix for the newly generated files");

    CLI11_PARSE(app, argc, argv);

    const auto in_json_path = utils::changeExtension(in_img_path, ".geojson");
    const auto out_tiff_path = utils::insertSuffixExtension(in_img_path, suffix);
    const auto out_json_path = utils::insertSuffixExtension(in_json_path, suffix);

    fmt::println("Input tiff file: {}", in_img_path);
    fmt::println("Input JSON file: {}", in_json_path);

    if (!in_roi_list_path.empty())
       fmt::println("Input ROI list file: {}", in_roi_list_path);

    fmt::println("Output tiff: {}", out_tiff_path);
    fmt::println("Output json: {}", out_json_path);

	try {
	    PyramidTiffData::repack_rois_to_pyramid(in_img_path, in_json_path,
            out_tiff_path, out_json_path,
            in_roi_list_path);
    }
    catch (const std::exception& e) {
        fmt::println("Error: {}", e.what());
        return 1;
    }

    return 0;
}
