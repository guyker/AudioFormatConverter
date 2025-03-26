#pragma once

#include "rapidjson/document.h"
#include <optional>
#include <string>


namespace JsonUtils {
    // Attempts to parse a RapidJSON member into the specified type T
    // Returns std::optional<T> with the value if successful, std::nullopt if not
    template <typename T>
    std::optional<T> tryParseMember(const rapidjson::Value& obj, const char* key);
}