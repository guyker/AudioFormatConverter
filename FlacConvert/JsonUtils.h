#pragma once

#include "rapidjson/document.h"
#include <optional>
#include <string>


#include <iostream>
#include <fstream>
#include <sstream>
#include <locale>
#include <codecvt>


namespace JsonUtils {

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
    template std::optional<double> tryParseMember<double>(const rapidjson::Value&, const char*);
    template std::optional<bool> tryParseMember<bool>(const rapidjson::Value&, const char*);



    std::string valueToString(const rapidjson::Value& value);
    std::string escapeJsonString(const std::string& input);
}