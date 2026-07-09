#include "OmeTiffPyramid.h"
#include "PolygonData.h"

#include <fstream>

#include <fstream>
#include <string>
#include <cctype>
#include <string_view>
#include <filesystem>
#include <stdexcept>

#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <nlohmann/json.hpp>
#include <simdjson.h>

// ---- Minimal structural JSON scanner (no value parsing, no DOM) ----
class JsonStructureScanner {
public:
    explicit JsonStructureScanner(const std::string& data) : data_(data.data()), size_(data.size()) {}

    void printStructure(int indent = 0) {
        skipWs();
        require(pos_ < size_, "unexpected end of input");
        printValue(indent);
    }

private:
    const char* data_;
    size_t size_;
    size_t pos_ = 0;

    void require(bool cond, const char* msg) {
        if (!cond) throw std::runtime_error(fmt::format("scan error at byte {}: {}", pos_, msg));
    }

    void skipWs() {
        while (pos_ < size_ && std::isspace(static_cast<unsigned char>(data_[pos_]))) pos_++;
    }

    void skipString() {
        require(data_[pos_] == '"', "expected string");
        pos_++;
        while (pos_ < size_) {
            char c = data_[pos_];
            if (c == '\\') { pos_ += 2; continue; }
            if (c == '"') { pos_++; return; }
            pos_++;
        }
        require(false, "unterminated string");
    }

    void skipBalanced(char open, char close) {
        require(data_[pos_] == open, "expected opening bracket");
        int depth = 0;
        while (pos_ < size_) {
            char c = data_[pos_];
            if (c == '"') { skipString(); continue; }
            if (c == open) { depth++; pos_++; continue; }
            if (c == close) { depth--; pos_++; if (depth == 0) return; continue; }
            pos_++;
        }
        require(false, "unbalanced brackets");
    }

    void skipValue() {
        skipWs();
        require(pos_ < size_, "unexpected end of input while skipping value");
        char c = data_[pos_];
        if (c == '"') { skipString(); }
        else if (c == '{') { skipBalanced('{', '}'); }
        else if (c == '[') { skipBalanced('[', ']'); }
        else {
            while (pos_ < size_) {
                char d = data_[pos_];
                if (d == ',' || d == ']' || d == '}' || std::isspace(static_cast<unsigned char>(d))) break;
                pos_++;
            }
        }
    }

    std::string_view scalarType() {
        char c = data_[pos_];
        if (c == '"') return "string";
        if (c == 't' || c == 'f') return "bool";
        if (c == 'n') return "null";
        return "number";
    }

    size_t countArrayItems() {
        size_t saved = pos_;
        size_t count = 0;
        while (true) {
            skipWs();
            skipValue();
            count++;
            skipWs();
            require(pos_ < size_, "unexpected end of input while counting array");
            if (data_[pos_] == ',') { pos_++; continue; }
            break;
        }
        pos_ = saved;
        return count;
    }

    void printValue(int indent) {
        std::string pad(indent * 2, ' ');
        skipWs();
        char c = data_[pos_];

        if (c == '{') {
            pos_++;
            skipWs();
            if (data_[pos_] == '}') { pos_++; fmt::println("{}{{}} (empty object)", pad); return; }
            while (true) {
                skipWs();
                require(data_[pos_] == '"', "expected object key");
                size_t keyStart = pos_ + 1;
                skipString();
                std::string_view key(data_ + keyStart, pos_ - keyStart - 1);

                skipWs();
                require(data_[pos_] == ':', "expected ':'");
                pos_++;
                skipWs();

                char vc = data_[pos_];
                if (vc == '{' || vc == '[') {
                    fmt::println("{}{}:", pad, key);
                    printValue(indent + 1);
                }
                else {
                    fmt::println("{}{}: {}", pad, key, scalarType());
                    skipValue();
                }

                skipWs();
                require(pos_ < size_, "unexpected end of input in object");
                if (data_[pos_] == ',') { pos_++; continue; }
                if (data_[pos_] == '}') { pos_++; break; }
                require(false, "expected ',' or '}'");
            }
        }
        else if (c == '[') {
            pos_++;
            skipWs();
            if (data_[pos_] == ']') { pos_++; fmt::println("{}[] (empty list)", pad); return; }

            size_t count = countArrayItems();
            fmt::println("{}List of {} items, e.g.:", pad, count);

            printValue(indent + 1);
            skipWs();
            while (data_[pos_] == ',') {
                pos_++;
                skipValue();
                skipWs();
            }
            require(data_[pos_] == ']', "expected ']'");
            pos_++;
        }
        else {
            fmt::println("{}{}", pad, scalarType());
            skipValue();
        }
    }
};



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

    static void printStructure(const nlohmann::json& j, const int indent = 0) {
        std::string pad(indent * 2, ' ');

        switch (j.type()) {
        case nlohmann::json::value_t::object: {
            for (auto it = j.begin(); it != j.end(); ++it) {
                const auto& val = it.value();
                if (val.is_object() || val.is_array()) {
                    fmt::println("{}{}:", pad, it.key());
                    printStructure(val, indent + 1);
                }
                else {
                    fmt::println("{}{}: {}", pad, it.key(), val.type_name());
                }
            }
            break;
        }
        case nlohmann::json::value_t::array: {
            if (j.empty()) {
                fmt::println("{}[] (empty list)", pad);
                break;
            }
            fmt::println("{}List of {} items, e.g.:", pad, j.size());
            // just show the structure of the first element as representative
            printStructure(j.front(), indent + 1);
            break;
        }
        default:
            fmt::println("{}{}", pad, j.type_name());
            break;
        }
    }

    std::string_view typeName(simdjson::dom::element_type t) {
        using et = simdjson::dom::element_type;
        switch (t) {
        case et::ARRAY:  return "array";
        case et::OBJECT: return "object";
        case et::INT64:  return "int64";
        case et::UINT64: return "uint64";
        case et::DOUBLE: return "double";
        case et::STRING: return "string";
        case et::BOOL:   return "bool";
        case et::NULL_VALUE: return "null";
        default: return "unknown";
        }
    }

    static void printStructure2(simdjson::dom::element j, const int indent = 0) {
        std::string pad(indent * 2, ' ');

        switch (j.type()) {
        case simdjson::dom::element_type::OBJECT: {
            simdjson::dom::object obj = j.get_object();
            for (auto [key, val] : obj) {
                if (val.type() == simdjson::dom::element_type::OBJECT ||
                    val.type() == simdjson::dom::element_type::ARRAY) {
                    fmt::println("{}{}:", pad, key);
                    printStructure2(val, indent + 1);
                }
                else {
                    fmt::println("{}{}: {}", pad, key, typeName(val.type()));
                }
            }
            break;
        }
        case simdjson::dom::element_type::ARRAY: {
            simdjson::dom::array arr = j.get_array();
            size_t count = arr.size();
            if (count == 0) {
                fmt::println("{}[] (empty list)", pad);
                break;
            }
            fmt::println("{}List of {} items, e.g.:", pad, count);
            printStructure2(*arr.begin(), indent + 1);
            break;
        }
        default:
            fmt::println("{}{}", pad, typeName(j.type()));
            break;
        }
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
        std::ifstream file(json_path, std::ios::binary);
        if (!file.is_open()) {
            fmt::println("Error: could not open {}", json_path);
            return 1;
        }

        // Read the whole file into memory in one go
        const auto size = std::filesystem::file_size(json_path);
        std::string buffer(size, '\0');
        file.read(buffer.data(), static_cast<std::streamsize>(size));
        if (!file) {
            fmt::println("Error: failed to read full file (read {} of {} bytes)", file.gcount(), size);
            return 1;
        }

        JsonStructureScanner scanner(buffer);
        scanner.printStructure();
    }
    catch (const std::exception& e) {
        fmt::println("Error: {}", e.what());
        return 1;
    }

	try {
        // read a JSON file
        std::ifstream loadFile(json_path, std::ios::in);
        if (!loadFile.is_open()) return 1;
        //const nlohmann::json geojson = nlohmann::json::parse(loadFile);

        //// read image
        //const PyramidTiffData::OmeTiffPyramid tiffReader(img_path);
        //tiffReader.print_info();

        //// read polygon mask
        //const PyramidTiffData::PolygonData jsonReader(json_path, tiffReader.series().width, tiffReader.series().height);
        //jsonReader.print_info();

        //// write images
        //constexpr size_t current_series = 0;
        //const auto current_channel = tiffReader.num_levels(current_series) - 1;
        //const PyramidTiffData::Image single_level = tiffReader.read_level(current_series, current_channel);

        //PyramidTiffData::write_to_disk_as_single_page_tiffs(single_level, fmt::format("./output_channels_level_{}", current_channel));
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
