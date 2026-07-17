#include "OmeTiffPyramid.h"
#include "PolygonData.h"

#include <fstream>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <jsoncons/basic_json.hpp>
#include <jsoncons/json_cursor.hpp>
#include <jsoncons/json_encoder.hpp>

namespace jsoncons {
    template<class Json>
    struct json_type_traits<Json, PyramidTiffData::Point2D> {
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

        static Json to_json(const PyramidTiffData::Point2D& p) {
            Json j(json_array_arg);
            j.push_back(p.x);
            j.push_back(p.y);
            return j;
        }
    };
}

using namespace jsoncons;

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

    static void create_output_file(const std::vector<ojson>& features_buffer, const std::filesystem::path& outfilepath, int file_number) {
        const auto filename = outfilepath / fmt::format("output_{}.geojson", file_number);

        std::ofstream output_file(filename);
        json_stream_encoder encoder(output_file);

        encoder.begin_object();
        encoder.key("type");
        encoder.string_value("FeatureCollection");
        encoder.key("features");
        encoder.begin_array();

        for (const auto& feature : features_buffer) {
            feature.dump(encoder);

            if (feature.contains("geometry") && feature["geometry"].contains("coordinates")) {

                const auto& coords = feature["geometry"]["coordinates"];

                if (coords.is_array() && !coords.empty()) {
                    try {
                        const auto points = coords[0].as<std::vector<PyramidTiffData::Point2D>>();
                    }
                    catch (const std::exception& e) {
                       fmt::println("Error parsing coordinates: {}", e.what());
                    }
                }
            }
        }

        encoder.end_array();
        encoder.end_object();
        encoder.flush();

        output_file.close();

        fmt::println("Created {} with {} features", filename, features_buffer.size());
    }

    static void split_json_file(const std::filesystem::path& json_path)
    {
        std::ifstream input_file(json_path);

        json_stream_cursor cursor(input_file);
        json_decoder<ojson> decoder;

        std::vector<ojson> features_buffer;
        constexpr int MAX_FEATURES_PER_FILE = 100;
        int file_number = 1;
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

                    features_buffer.push_back(feature);

                    fmt::println("Feature {} collected", features_buffer.size());

                    // When buffer reaches 10 features, write to file and reset
                    if (features_buffer.size() >= MAX_FEATURES_PER_FILE) {
                        utils::create_output_file(features_buffer, json_path.parent_path(), file_number);
                        features_buffer.clear();
                        file_number++;
                    }
                }
                break;
            }

            case staj_event_type::end_array:
            {
                if (in_features_array) {
                    // Write remaining features to a file at the end
                    if (!features_buffer.empty()) {
                        utils::create_output_file(features_buffer, json_path.parent_path(), file_number);
                        features_buffer.clear();
                    }
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

    }
   
	static void extract_roi_json_file(const std::filesystem::path& json_path)
    {
        std::ifstream input_file(json_path);

        json_stream_cursor cursor(input_file);
        json_decoder<ojson> decoder;

        std::vector<ojson> features_buffer;
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

                    const auto& props = feature.at("properties");
                    if (!props.contains("classification")) break;

                    const auto& classification = props.at("classification");
                    if (!classification.contains("name")) break;

                    if (classification.at("name").as<std::string>() == "ROI")
                    {
                        features_buffer.push_back(feature);

                    }

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

        utils::create_output_file(features_buffer, json_path.parent_path(), 99);
    }

    static void copy_tiff_file(const std::filesystem::path& img_path, const std::filesystem::path& json_path)
    {
        // read a JSON file
        std::ifstream loadFile(json_path, std::ios::in);
        if (!loadFile.is_open()) return;

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
        utils::copy_tiff_file(img_path, json_path);
        utils::split_json_file(json_path);
        utils::extract_roi_json_file(json_path);
    }
    catch (const std::exception& e) {
        fmt::println("Error: {}", e.what());
        return 1;
    }
    return 0;
}
