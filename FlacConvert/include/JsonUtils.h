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

#include "CommonUtils.h"


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


    // Function to get value by key, returns nullptr if key doesn't exist
    const std::string* getValueByKey(const std::map<std::string, std::string, CaseInsensitiveCompare>& map, const std::string& key);

    //Represents dynamic metadata tags (key-value pairs)
    // Key-value map, keys lowercase (e.g., "artist"), values strings (e.g., "The Beatles"), from file metadata
    //using Tags = std::map<std::string, std::string>;
    using Tags = std::map<std::string, std::string, CaseInsensitiveCompare>;


    // Attempts to parse a RapidJSON member into the specified type T
    // Returns std::optional<T> with the value if successful, std::nullopt if not
 //   template <typename T>
  //  std::optional<T> tryParseMember(const rapidjson::Value& obj, const char* key);

    // Explicit template instantiations (optional, see notes)
//    template std::optional<std::optional<std::string>> tryParseMember<std::optional<std::string>>(const rapidjson::Value&, const char*);
    //template std::optional<std::optional<double>> tryParseMember<std::optional<double>>(const rapidjson::Value&, const char*);

    // template std::optional<std::string> tryParseMember<std::string>(const rapidjson::Value&, const char*);
    // template std::optional<std::wstring> tryParseMember<std::wstring>(const rapidjson::Value&, const char*);

    // template std::optional<int> tryParseMember<int>(const rapidjson::Value&, const char*);
    // template std::optional<long> tryParseMember<long>(const rapidjson::Value&, const char*);
    // template std::optional<int64_t> tryParseMember<int64_t>(const rapidjson::Value&, const char*);
    // template std::optional<uint64_t> tryParseMember<uint64_t>(const rapidjson::Value&, const char*);
    // template std::optional<long long> tryParseMember<long long>(const rapidjson::Value&, const char*);
    // template std::optional<double> tryParseMember<double>(const rapidjson::Value&, const char*);
    // template std::optional<bool> tryParseMember<bool>(const rapidjson::Value&, const char*);




    // Generic function to parse a RapidJSON member into a specified type
    template <typename T>
    std::optional<T> tryParseMember(const rapidjson::Value& obj, const char* key_) {

        if (!obj.IsObject())
        {
            return std::nullopt;
        }

        const rapidjson::Value* valuePtr = nullptr;

        // Try case-sensitive lookup first
        if (obj.HasMember(key_)) {
            valuePtr = &obj[key_];
        }
        else {
            // Try case-insensitive lookup
            for (auto it = obj.MemberBegin(); it != obj.MemberEnd(); ++it) {
                if (it->name.IsString() && _stricmp(it->name.GetString(), key_) == 0) {
                    valuePtr = &it->value;
                    break;
                }
            }
        }

        if (!valuePtr)
        {
            return std::nullopt;
        }

        const auto& value = *valuePtr;

        if constexpr (std::is_same_v<T, std::optional<std::string>>) {
            if (value.IsString()) {
                return value.GetString();
            }
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            if (value.IsString()) {
                return value.GetString();
            }
        }
        else if constexpr (std::is_same_v<T, std::wstring>) {
            if (value.IsString()) {
                return CommonUtils::utf8ToWstring(value.GetString());
            }
        }        
        else if constexpr (std::is_same_v<T, int>) {
            if (value.IsInt()) {
                return value.GetInt();
            }
        }
        else if constexpr (std::is_same_v<T, long>) {
            if (value.IsInt64()) {
                return static_cast<long>(value.GetInt64());
            }
            else if (value.IsString()) {
                try {
                    return std::stol(value.GetString());
                }
                catch (const std::exception&) {
                    return std::nullopt;
                }
            }
        }
        else if constexpr (std::is_same_v<T, uint64_t>) {
            if (value.IsInt64()) {
                return value.GetInt64();
            }
        }
        else if constexpr (std::is_same_v<T, int64_t>) {
            if (value.IsInt64()) {
                return value.GetInt64();
            }
        }
        else if constexpr (std::is_same_v<T, long long>) {
            if (value.IsInt64()) {
                return value.GetInt64();
            }
        }
        else if constexpr (std::is_same_v<T, double>) {
            if (value.IsDouble()) {
                return value.GetDouble();
            }
        }
        else if constexpr (std::is_same_v<T, bool>) {
            if (value.IsBool()) {
                return value.GetBool();
            }
        }
        // Add more types as needed (e.g., uint, float)

        return std::nullopt; // Type mismatch or unsupported type
    }


    std::string TypeToString(rapidjson::Type type);
    Tags GetKeyValueMap(const rapidjson::Value& jsonValue);

    std::string valueToString(const rapidjson::Value& value);
    std::string escapeJsonString(const std::string& input);
}