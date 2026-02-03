#include "FilteredReaderHandler.h"
#include "rapidjson/error/en.h"

namespace json {

class FileStreamAdapter {
public:
    typedef char Ch;

    explicit FileStreamAdapter(File& file) : file_(file) {}

    [[nodiscard]] Ch Peek() const {
        const int c = file_.peek();
        return c == -1 ? '\0' : static_cast<Ch>(c);
    }

    Ch Take() const {
        int c = file_.read();
        return c == -1 ? '\0' : static_cast<Ch>(c);
    }

    [[nodiscard]] size_t Tell() const {
        return file_.position();
    }

    Ch* PutBegin() { return 0; }
    void Put(Ch) {}
    void Flush() {}
    size_t PutEnd(Ch*) { return 0; }

private:
    File& file_;
};

FilteredReaderHandler::FilteredReaderHandler() : nestingLevel(0) {
    bufferStack.emplace_back();
    writerStack.emplace_back(bufferStack.back());
}

FilteredReaderHandler::Filter::Filter(std::vector<std::string> p, FilterCallback cb, bool include)
        : path(std::move(p)), callback(std::move(cb)), includeInParent(include) {}

void FilteredReaderHandler::addFilter(std::vector<std::string> path, FilterCallback callback, bool includeInParent) {
    filters.emplace_back(std::move(path), std::move(callback), includeInParent);
}

bool FilteredReaderHandler::parse(const char* json, size_t length) {
    rapidjson::Reader reader;
    rapidjson::StringStream ss(json);
    rapidjson::ParseResult result = reader.Parse(ss, *this);
    if (result.IsError()) {
        printf("JSON parse error: %s (%u)",
                rapidjson::GetParseError_En(result.Code()), result.Offset());
    }
    return result;
}

bool FilteredReaderHandler::parse(File f) {
    FileStreamAdapter fsa(f);
    rapidjson::Reader reader;
    rapidjson::ParseResult result = reader.Parse(fsa, *this);
    if (result.IsError()) {
        printf("JSON parse error: %s (%u)",
               rapidjson::GetParseError_En(result.Code()), result.Offset());
    }
    return result;
}

bool FilteredReaderHandler::Null() { !inTargetPath || writerStack.back().Null();  return true; }
bool FilteredReaderHandler::Bool(bool b) { !inTargetPath || writerStack.back().Bool(b); return true; }
bool FilteredReaderHandler::Int(int i) { !inTargetPath || writerStack.back().Int(i); return true; }
bool FilteredReaderHandler::Uint(unsigned u) { !inTargetPath || writerStack.back().Uint(u); return true; }
bool FilteredReaderHandler::Int64(int64_t i) { !inTargetPath || writerStack.back().Int64(i); return true; }
bool FilteredReaderHandler::Uint64(uint64_t u) { !inTargetPath || writerStack.back().Uint64(u); return true; }
bool FilteredReaderHandler::Double(double d) { !inTargetPath || writerStack.back().Double(d); return true; }
bool FilteredReaderHandler::RawNumber(const char* str, rapidjson::SizeType length, bool copy) { !inTargetPath || writerStack.back().RawNumber(str, length); return true; }
bool FilteredReaderHandler::String(const char* str, rapidjson::SizeType length, bool copy) { !inTargetPath || writerStack.back().String(str, length); return true; }

bool FilteredReaderHandler::StartObject() {
    bool shouldIncludeInParent;

    if (inTargetPath) {
        writerStack.back().StartObject();
    } else if (isTargetPath(shouldIncludeInParent)) {
        inTargetPath = true;
        targetPathIndex = nestingLevel;
        writerStack.back().StartObject();
    }
    nestingLevel++;
    return true;
}

bool FilteredReaderHandler::Key(const char* str, rapidjson::SizeType length, bool copy) {
    if (nestingLevel > currentPath.size()) {
        currentPath.push_back(std::string(str, length));
    } else {
        currentPath[nestingLevel - 1] = std::string(str, length);
    }

    if (!inTargetPath)
        return true;

    writerStack.back().Key(str, length);

    return true;
}

bool FilteredReaderHandler::EndObject(rapidjson::SizeType memberCount) {
    nestingLevel--;

    while (currentPath.size() > nestingLevel) {
        currentPath.pop_back();
    }

    if (inTargetPath) {
        writerStack.back().EndObject();
        if (nestingLevel == targetPathIndex) {
            processFilters(true);
            // clear state
            bufferStack.clear();
            writerStack.clear();

            bufferStack.emplace_back();
            writerStack.emplace_back(bufferStack.back());

            inTargetPath = false;
            targetPathIndex = -1;
        }
    }

    return true;
}

bool FilteredReaderHandler::StartArray() {
    bool shouldIncludeInParent;

    if (inTargetPath) {
        writerStack.back().StartArray();
    } else if (isTargetPath(shouldIncludeInParent)) {
        inTargetPath = true;
        targetPathIndex = nestingLevel;
        writerStack.back().StartArray();
    }
    nestingLevel++;

    return true;
}

bool FilteredReaderHandler::EndArray(rapidjson::SizeType elementCount) {
    nestingLevel--;

    while (currentPath.size() > nestingLevel) {
        currentPath.pop_back();
    }

    if (inTargetPath) {
        writerStack.back().EndArray();
        if (nestingLevel == targetPathIndex) {
            processFilters(false);
            // clear state
            bufferStack.clear();
            writerStack.clear();

            bufferStack.emplace_back();
            writerStack.emplace_back(bufferStack.back());

            inTargetPath = false;
            targetPathIndex = -1;
        }
    }

    return true;
}

bool FilteredReaderHandler::isTargetPath(bool& shouldIncludeInParent) {
    for (const auto& filter : filters) {
        if (filter.path.size() == currentPath.size() &&
            std::equal(filter.path.begin(), filter.path.end(), currentPath.begin())) {
            shouldIncludeInParent = filter.includeInParent;
            return true;
        }
    }
    shouldIncludeInParent = false;
    return false;
}

void FilteredReaderHandler::processFilters(bool isEndOfObject) {
    if (bool shouldIncludeInParent; isTargetPath(shouldIncludeInParent)) {
        std::string json = bufferStack.back().GetString();
        for (const auto& filter : filters) {
            if (filter.path == currentPath) {
                filter.callback(filter.path, json);
                break;
            }
        }
        /*

        if (shouldIncludeInParent && writerStack.size() > 1) {
            if (isEndOfObject) {
                writerStack[writerStack.size() - 2].RawValue(json.c_str(), json.length(), rapidjson::kObjectType);
            } else {
                writerStack[writerStack.size() - 2].RawValue(json.c_str(), json.length(), rapidjson::kArrayType);
            }
        }
        */
    }
}

} // namespace json
