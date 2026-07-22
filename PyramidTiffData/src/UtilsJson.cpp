#include "UtilsJson.h"

#include <fmt/format.h>

namespace PyramidTiffData {

    MaskType getMaskType(const jsoncons::ojson& feat)
    {
        if (!feat.contains("properties"))
            return MaskType::None;

        const auto& props = feat.at("properties");

        if (!props.contains("objectType"))
            return MaskType::None;

        if (props.at("objectType").as<std::string>() == "cell")
            return MaskType::Cell;

        if (!props.contains("classification"))
            return MaskType::None;

        const auto& classification = props.at("classification");

        if (!classification.contains("name"))
            return MaskType::None;

        const std::string maskName = classification.at("name").as<std::string>();

        if (maskName == "ROI")
            return MaskType::Roi;

        if (maskName == "TISSUE")
            return MaskType::Tissue;

        return MaskType::None;
    }

    void parseName(
        const jsoncons::ojson& feat,
        std::string& name,
        const std::string& prefix,
        int& counter)
    {
        if (feat.at("properties").contains("name")) {
            name = feat.at("properties").at("name").as<std::string>();
        }
        else {
            name = fmt::format("{} {}", prefix, counter++);
        }
    }

    void parseNameID(
        const jsoncons::ojson& feat,
        std::string& name,
        const std::string& prefix,
        int& counter)
    {
        if (feat.contains("id")) {
            name = feat.at("id").as<std::string>();
        }
        else {
            name = fmt::format("{} {}", prefix, counter++);
        }
    }

    void parseGeometry(
        const jsoncons::ojson& feat,
        std::vector<Point2D>& poly_points,
        const std::string& geometryKey)
    {

        if (!(feat.contains(geometryKey) &&
            feat.at(geometryKey).contains("coordinates")))
            return;

        const auto& base_coords =
            feat.at(geometryKey).at("coordinates");

        if (base_coords.empty())
            return;

        const auto& first_elem = base_coords.at(0);

        if (first_elem.empty())
            return;

        // Default to standard Polygon level
        const jsoncons::ojson* coordinates = &first_elem;

        // Check an extra level of nesting.
        // If first_elem[0][0] is an array,
        // we are nested one level too deep.
        if (first_elem[0].is_array() &&
            !first_elem[0].empty() &&
            first_elem[0][0].is_array())
        {
            coordinates = &first_elem.at(0);
        }

        poly_points.reserve(coordinates->size());

        for (const auto& coords : coordinates->array_range()) {
            poly_points.push_back(coords.as<Point2D>());
        }
        
    }

    void parseColor(
        const jsoncons::ojson& feat,
        std::array<uint8_t, 3>& color)
    {
        if (feat.at("properties").contains("classification") &&
            feat.at("properties").at("classification").contains("color"))
        {
            const auto feat_color =
                feat.at("properties")
                .at("classification")
                .at("color")
                .as<std::vector<uint8_t>>();

            color = {
                feat_color[0],
                feat_color[1],
                feat_color[2]
            };
        }
        else {
            color = { 128, 128, 128 };
        }
    }

} // PyramidTiffData
