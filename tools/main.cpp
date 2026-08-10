#include "UtilsJson.h"
#include "UtilsRoiArrangement.h"

#include <filesystem>
#include <string>
#include <string_view>

#include <fmt/base.h>
#include <fmt/std.h>

namespace utils
{
    static std::filesystem::path changeExtension(
        const std::filesystem::path& p,
        const std::string_view ext)
    {
        auto result = p;
        result.replace_extension(ext);
        return result;
    }

    static std::filesystem::path insertSuffixExtension(
        const std::filesystem::path& p,
        const std::string& suffix)
    {
        return p.parent_path() /
            (p.stem().string() + suffix + p.extension().string());
    }
}

// Reads a tiff file that contains an image pyramid
// and writes each chanel of the hightest level to disk
int main(int argc, char* argv[]) {

    if (argc !=2 && argc != 3) {
        fmt::println("Usage:\n    pyramidstackmerge <path_to_tiff_file>");
        fmt::println("            pyramidstackmerge <path_to_tiff_file> <path_to_roi_list>");
        return 1;
    }
	
    const std::filesystem::path img_path = argv[1];
    const std::filesystem::path roi_list_path = argc == 3 ? argv[2] : "";
    const std::filesystem::path json_path = utils::changeExtension(img_path, ".geojson");

    fmt::println("Reading tiff file: {}", img_path);
    fmt::println("Reading JSON file: {}", json_path);

    if (!roi_list_path.empty())
        fmt::println("Reading ROI list file: {}", roi_list_path);

    const auto out_tiff_file = utils::insertSuffixExtension(img_path, "new3");
    const auto out_json_file = utils::insertSuffixExtension(json_path, "new3");

    fmt::println("Output tiff: {}", out_tiff_file);
    fmt::println("Output json: {}", out_json_file);

	try {
	    PyramidTiffData::repack_rois_to_pyramid(img_path, json_path,
            out_tiff_file, out_json_file,
            roi_list_path);
    }
    catch (const std::exception& e) {
        fmt::println("Error: {}", e.what());
        return 1;
    }

    return 0;
}
