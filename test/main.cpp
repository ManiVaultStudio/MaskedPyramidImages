#include "OmeTiffPyramid.h"
#include "PolygonData.h"

#include <fstream>

#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <nlohmann/json.hpp>

namespace utils
{
    static std::filesystem::path change_extension(
        const std::filesystem::path& p,
        const std::string_view ext)
    {
        auto result = p;
        result.replace_extension(ext);
        return result;
    }
}

// Reads a tiff file that contains an image pyramid
// and writes each chanel of the hightest level to disk
int main(int argc, char* argv[]) {

    if (argc != 2) {
        fmt::println("Usage:\n    tifftest <path_to_tiff_file>\nYou did not provide a tiff file.");
        return 1;
    }
	
    const std::filesystem::path img_path = argv[1];
    const std::filesystem::path json_path = utils::change_extension(img_path, ".geojson");
    fmt::println("Reading file: {}", img_path);
    fmt::println("JSON file: {}", json_path);

	try {
        // read a JSON file
        std::ifstream loadFile(json_path, std::ios::in);
        if (!loadFile.is_open()) return 1;
        nlohmann::json geojson = nlohmann::json::parse(loadFile);

        // read image
        const PyramidTiffData::OmeTiffPyramid tiffReader(img_path);
        tiffReader.print_info();

        // read polygon mask
        const PyramidTiffData::PolygonData jsonReader(json_path, tiffReader.series().width, tiffReader.series().height);
        jsonReader.print_info();

        // write images
        constexpr size_t current_series = 0;
        const auto current_channel = tiffReader.num_levels(current_series) - 1;
        const PyramidTiffData::Image single_level = tiffReader.read_level(current_series, current_channel);

        PyramidTiffData::write_to_disk_as_single_page_tiffs(single_level, fmt::format("./output_channels_level_{}", current_channel));
    }
	catch (const nlohmann::detail::parse_error& err) {
	   fmt::print("PyramidImageData::scan: json parse error: {}", err.what());
	   return 1;
	}
    catch (const std::exception& e) {
        fmt::println("Error: {}", e.what());
        return 1;
    }
    return 0;
}
