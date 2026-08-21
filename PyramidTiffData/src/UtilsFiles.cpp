#include "UtilsFiles.h"

#include "PolygonData.h"

#include <rapidcsv.h>

#include <unordered_map>

namespace PyramidTiffData {

    std::filesystem::path changeExtension(
    const std::filesystem::path& p,
    const std::string_view ext)
    {
        const std::string filename = p.filename().string();

        // Find the first dot (skipping index 0 to handle hidden files like .gitignore)
        const size_t first_dot = filename.find('.', 1);

        if (first_dot == std::string::npos) {
            // If there's no extension at all, use standard behavior
            auto result = p;
            result.replace_extension(ext);
            return result;
        }

        // Extract the stem (everything before the first dot)
        const std::string stem = filename.substr(0, first_dot);

        // Prepare the new extension: ensure it starts with a dot if not empty
        std::string formatted_ext(ext);
        if (!formatted_ext.empty() && formatted_ext[0] != '.') {
            formatted_ext.insert(0, ".");
        }

        // Reconstruct the path: Parent Dir + Stem + New Extension
        return p.parent_path() / (stem + formatted_ext);
    }

    std::filesystem::path insertSuffixExtension(
        const std::filesystem::path& p,
        const std::string& suffix)
    {
        const std::string filename = p.filename().string();

        // Find the first dot. 
        // We start searching at index 1 to avoid treating hidden files 
        // (e.g., .gitignore) as having an extension at the start.
        const size_t first_dot = filename.find('.', 1);

        if (first_dot == std::string::npos) {
            // No extension found, just append to the end
            return p.parent_path() / (filename + suffix);
        }

        const std::string stem = filename.substr(0, first_dot);
        const std::string extension = filename.substr(first_dot);

        return p.parent_path() / (stem + suffix + extension);
    }

    void writeClusterIdsToCsv(const std::filesystem::path& p, const int64_t numCells, const int64_t numClusters,
        const PolygonData& polygons, const std::vector<std::vector<uint32_t>>& cellClusterIds)
    {
        rapidcsv::Document csv("", rapidcsv::LabelParams(0, -1));

        const auto& cellCentroids = polygons.centroids_cell();
        const auto& cellNames = polygons.names_cell();

        assert(cellCentroids.size() == numCells);
        assert(cellNames.size() == numCells);


        std::vector<double> centroidX;
        std::vector<double> centroidY;
        std::vector<std::string> imageNames;
        std::vector<int64_t> clusterIDs;

        centroidX.reserve(numCells);
        centroidY.reserve(numCells);
        imageNames.reserve(numCells);
        clusterIDs.reserve(numCells);

        auto mostFrequent = [numClusters](const std::vector<uint32_t>& v) -> int64_t {
            if (v.empty())
                return std::numeric_limits<int64_t>::max();

            std::unordered_map<uint32_t, uint32_t> counts;
            counts.reserve(numClusters);

            uint32_t best = v[0], bestCount = 0;
            for (uint32_t x : v) {
                uint32_t c = ++counts[x];
                if (c > bestCount) {
                    bestCount = c;
                    best = x;
                }
            }
            return static_cast<int64_t>(best);
            };

        for (int64_t numCell = 0; numCell < numCells; ++numCell)
        {
            centroidX.push_back(cellCentroids[numCell].x);
            centroidY.push_back(cellCentroids[numCell].y);
            imageNames.push_back(polygons.roi_name_cell(numCell));
            clusterIDs.push_back(mostFrequent(cellClusterIds[numCell]));
        }

        size_t columnIdx = 0;
        csv.InsertColumn<double>(columnIdx++, centroidX, "X");
        csv.InsertColumn<double>(columnIdx++, centroidY, "Y");
        csv.InsertColumn<std::string>(columnIdx++, imageNames, "Image");
        csv.InsertColumn<int64_t>(columnIdx++, clusterIDs, "Cluster");
        csv.InsertColumn<std::string>(columnIdx++, cellNames, "Object_ID");

        csv.Save(p.generic_string());
    }

} // PyramidTiffData
