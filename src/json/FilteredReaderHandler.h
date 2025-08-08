#ifndef STREAMING_JSON_PARSER_H
#define STREAMING_JSON_PARSER_H

#include "rapidjson/reader.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"
#include <Arduino.h>
#include "LittleFS.h"
#include <string>
#include <vector>
#include <functional>

namespace json {
/**
 * @brief Handler for filtering and processing JSON data
 *
 * This class provides functionality to parse JSON data and apply filters based on specified paths.
 * It uses RapidJSON for parsing and allows for custom callbacks when certain paths are encountered.
 */
class FilteredReaderHandler {
public:
    using FilterCallback = std::function<void(const std::vector<std::string>&, const std::string&)>;

    FilteredReaderHandler();

    void addFilter(std::vector<std::string> path, FilterCallback callback, bool includeInParent = false);

    bool parse(const char* json, size_t length);
    bool parse(File f);


    bool Null();
    bool Bool(bool b);
    bool Int(int i);
    bool Uint(unsigned u);
    bool Int64(int64_t i);
    bool Uint64(uint64_t u);
    bool Double(double d);
    bool RawNumber(const char* str, rapidjson::SizeType length, bool copy);
    bool String(const char* str, rapidjson::SizeType length, bool copy);
    bool StartObject();
    bool Key(const char* str, rapidjson::SizeType length, bool copy);
    bool EndObject(rapidjson::SizeType memberCount);
    bool StartArray();
    bool EndArray(rapidjson::SizeType elementCount);

private:
    struct Filter {
        std::vector<std::string> path;
        FilterCallback callback;
        bool includeInParent;

        Filter(std::vector<std::string> p, FilterCallback cb, bool include);
    };

    std::vector<Filter> filters;
    std::vector<std::string> currentPath;
    std::vector<rapidjson::StringBuffer> bufferStack;
    std::vector<rapidjson::Writer<rapidjson::StringBuffer>> writerStack;
    int nestingLevel = 0;

    bool inTargetPath = false;
    int  targetPathIndex = -1;

    bool isTargetPath(bool& shouldIncludeInParent);
    void processFilters(bool isEndOfObject);
};

};

#endif // STREAMING_JSON_PARSER_H