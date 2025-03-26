#include "JsonUtils.h"

#include <string>
#include <optional>
#include <type_traits>


#include "rapidjson/rapidjson.h" 
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"


namespace JsonUtils {
    // Generic function to parse a RapidJSON member into a specified type
    template <typename T>
    std::optional<T> tryParseMember(const rapidjson::Value& obj, const char* key) {
        if (!obj.HasMember(key)) {
            return std::nullopt; // Member doesn’t exist
        }

        const auto& value = obj[key];

        if constexpr (std::is_same_v<T, std::string>) {
            if (value.IsString()) {
                return value.GetString();
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


    // Explicit template instantiations (optional, see notes)
    template std::optional<std::string> tryParseMember<std::string>(const rapidjson::Value&, const char*);
    template std::optional<int> tryParseMember<int>(const rapidjson::Value&, const char*);
    template std::optional<long> tryParseMember<long>(const rapidjson::Value&, const char*);
    template std::optional<double> tryParseMember<double>(const rapidjson::Value&, const char*);
    template std::optional<bool> tryParseMember<bool>(const rapidjson::Value&, const char*);
}




