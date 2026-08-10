#include "UtilsJson.h"
#include "UtilsRoiArrangement.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <fmt/base.h>
#include <fmt/ranges.h>
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
    
    static std::vector<std::string> readRoiList(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
        {
            fmt::println("readFile: file does not exist: {}", path);
            return {};
        }

        std::vector<std::string> lines;

        std::ifstream file(path);

        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }

        return lines;
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

    fmt::println("Reading file: {}", img_path);
    fmt::println("JSON file: {}", json_path);
    if (!roi_list_path.empty())
        fmt::println("ROI list file: {}", roi_list_path);

    constexpr bool VERBOSE = true;

	try {
        const auto roiList = utils::readRoiList(roi_list_path);
        fmt::print("{}\n", fmt::join(roiList, "\n"));

	    PyramidTiffData::repack_rois_to_pyramid(img_path, json_path,
            utils::insertSuffixExtension(img_path, "new"),
            utils::insertSuffixExtension(json_path, "new"));
    }
    catch (const std::exception& e) {
        fmt::println("Error: {}", e.what());
        return 1;
    }
    return 0;
}
