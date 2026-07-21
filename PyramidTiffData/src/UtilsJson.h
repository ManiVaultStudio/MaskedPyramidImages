#pragma once

#include "UtilsTransform.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <jsoncons/basic_json.hpp>
#include <jsoncons/encode_json.hpp>
#include <jsoncons/json_cursor.hpp>
#include <jsoncons/json_type.hpp>

namespace jsoncons {
    template<class Json>
    struct json_type_traits<Json, PyramidTiffData::Point2D> {
        using allocator_type = typename Json::allocator_type;

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

        // Converts Point2D to JSON
        static Json to_json(const PyramidTiffData::Point2D& val) {
            Json j(json_array_arg);
            j.push_back(val.x);
            j.push_back(val.y);
            return j;
        }

        // Converts Point2D to JSON using a specific allocator
        static Json to_json(const PyramidTiffData::Point2D& val, const allocator_type& alloc) {
            Json j(json_array_arg, alloc);
            j.push_back(val.x);
            j.push_back(val.y);
            return j;
        }
    };
}
namespace PyramidTiffData {
    MaskType getMaskType(const jsoncons::json& feat);

    void parseName(
        const jsoncons::json& feat,
        std::vector<std::string>& names,
        const std::string& prefix,
        int& counter);

    void parseNameID(
        const jsoncons::json& feat,
        std::vector<std::string>& names,
        const std::string& prefix,
        int& counter);

    void parseGeometry(
        const jsoncons::json& feat,
        std::vector<std::vector<Point2D>>& polygons,
        const std::string& geometryKey = "geometry");

    inline void parseGeometryNucleus(
        const jsoncons::json& feat,
        std::vector<std::vector<Point2D>>& polygons)
    {
        parseGeometry(feat, polygons, "nucleusGeometry");
    }

    void parseColor(
        const jsoncons::json& feat,
        std::vector<std::array<uint8_t, 3>>& colors);
}
