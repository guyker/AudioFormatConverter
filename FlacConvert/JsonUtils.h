#pragma once

#include "rapidjson/document.h"
#include <optional>
#include <string>


#include <iostream>
#include <fstream>
#include <sstream>
#include <locale>
#include <codecvt>
#include <map>


namespace JsonUtils {

    struct CaseInsensitiveCompare {
        bool operator()(const std::string& a, const std::string& b) const {
            return std::lexicographical_compare(
                a.begin(), a.end(),
                b.begin(), b.end(),
                [](char c1, char c2) {
                    return tolower(c1) < tolower(c2);
                });
        }
    };

    //Represents dynamic metadata tags (key-value pairs)
    // Key-value map, keys lowercase (e.g., "artist"), values strings (e.g., "The Beatles"), from file metadata
    //using Tags = std::map<std::string, std::string>;
    using Tags = std::map<std::string, std::string, CaseInsensitiveCompare>;


    // Attempts to parse a RapidJSON member into the specified type T
    // Returns std::optional<T> with the value if successful, std::nullopt if not
    template <typename T>
    std::optional<T> tryParseMember(const rapidjson::Value& obj, const char* key);

    // Explicit template instantiations (optional, see notes)
    template std::optional<std::optional<std::string>> tryParseMember<std::optional<std::string>>(const rapidjson::Value&, const char*);
    template std::optional<std::optional<double>> tryParseMember<std::optional<double>>(const rapidjson::Value&, const char*);

    template std::optional<std::string> tryParseMember<std::string>(const rapidjson::Value&, const char*);
    template std::optional<std::wstring> tryParseMember<std::wstring>(const rapidjson::Value&, const char*);

    template std::optional<int> tryParseMember<int>(const rapidjson::Value&, const char*);
    template std::optional<long> tryParseMember<long>(const rapidjson::Value&, const char*);
    template std::optional<int64_t> tryParseMember<int64_t>(const rapidjson::Value&, const char*);
    template std::optional<uint64_t> tryParseMember<uint64_t>(const rapidjson::Value&, const char*);
    template std::optional<long long> tryParseMember<long long>(const rapidjson::Value&, const char*);
    template std::optional<double> tryParseMember<double>(const rapidjson::Value&, const char*);
    template std::optional<bool> tryParseMember<bool>(const rapidjson::Value&, const char*);

    std::string TypeToString(rapidjson::Type type);
    Tags GetKeyValueMap(const rapidjson::Value& jsonValue);

    std::string valueToString(const rapidjson::Value& value);
    std::string escapeJsonString(const std::string& input);
}