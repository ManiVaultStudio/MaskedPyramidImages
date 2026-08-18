#include "UtilsClusters.h"

#include "UtilsJson.h"

#include <fstream>

#include <fmt/base.h>

#include <jsoncons/json.hpp>
#include <jsoncons/json_cursor.hpp>

namespace PyramidTiffData {

    std::vector<CellStruct> readCellStructs(const std::filesystem::path& jsonFilePath,
        const uint32_t baseWidth, const uint32_t baseHeight)
    {
        std::ifstream f(jsonFilePath);
        if (!f.is_open()) {
            fmt::println("parseJson: could not open {}", jsonFilePath.string());
            return {};
        }

        std::vector<CellStruct> cellStructs;

        bool found_features = false;
        const auto cellPrefix = getMaskString(MaskType::Cell);

        try {
            jsoncons::json_stream_cursor cursor(f);

            bool in_features_array = false;

            int cellNameCounter = 0;
            int roiNameCounter = 0;
            std::string currentRoiName;

            const uintmax_t total_bytes = std::filesystem::file_size(jsonFilePath);
            jsoncons::json_decoder<jsoncons::ojson> decoder;

            fmt::println("Reading the json file...");

            auto last_pct = ProgressBarInit();
            for (; !cursor.done(); cursor.next())
            {
                const auto& event = cursor.current();
                switch (event.event_type())
                {
                case jsoncons::staj_event_type::key:
                {
                    const auto key = event.get<jsoncons::string_view>();
                    if (key == "features") {
                        in_features_array = true;
                        found_features = true;
                    }
                    break;
                }
                case jsoncons::staj_event_type::begin_object:
                {
                    if (!in_features_array) {
                        break; // e.g. the document's own root object
                    }

                    // Pull exactly one "feature" object into memory, process it,
                    // then let it go out of scope, the rest of the file stays unread.
                    cursor.read_to(decoder);
                    const jsoncons::ojson feature = decoder.get_result();

                    const auto maskType = getMaskType(feature);

                    if (maskType == MaskType::Roi) {
                        parseName(feature, currentRoiName, getMaskString(maskType), roiNameCounter);
                    }

                    if (maskType != MaskType::Cell)
                        break;

                    std::string cellName;
                    parseNameID(feature, cellName, cellPrefix, cellNameCounter);

                    std::vector<Point2D> cellCoordinates{};
                    parseGeometry(feature, cellCoordinates, "geometry");

                    cellStructs.push_back({
                        .centroid = computeCentroid(cellCoordinates),
                        .basePixels = rasterize_polygon(cellCoordinates, baseWidth, baseHeight),
                        .cellName = cellName,
                        .imageName = currentRoiName,
                        .clusterId = -1
                        });
                ;
                    break;
                }
                case jsoncons::staj_event_type::end_array:
                {
                    if (in_features_array) {
                        in_features_array = false;
                    }
                    break;
                }
                default:
                    break;
                }

                const auto pos = f.tellg();
                if (pos > 0) ProgressBarPrint(static_cast<std::uintmax_t>(pos), last_pct, total_bytes);
            }
            ProgressBarFinish();

        }
        catch (const std::exception& err) {
            fmt::println("parseJson error: {}", err.what());
        }

        f.close();

        if (!found_features) {
            fmt::println("PolygonData::parse_mask_annotations: json does not contain features field");
        }
         
        return cellStructs;
    }


} // PyramidTiffData
