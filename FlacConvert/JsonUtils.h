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
}