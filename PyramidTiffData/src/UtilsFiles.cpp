#include "UtilsFiles.h"

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

} // PyramidTiffData
