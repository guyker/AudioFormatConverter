#include "JsonUtils.h"

#include <string>
#include <optional>
#include <type_traits>
#include "CommonUtils.h"

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
#include <spdlog/spdlog.h>


namespace JsonUtils {

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

    // Parse Tags from a RapidJSON Value
    Tags GetKeyValueMap(const rapidjson::Value& jsonValue)
    {
        Tags resultTags;
        // Iterate over the object's members using MemberIterator
        for (rapidjson::Value::ConstMemberIterator itr = jsonValue.MemberBegin(); itr != jsonValue.MemberEnd(); ++itr) {
            // Get the key (tag name) as a string
            std::string key = itr->name.GetString();

            // Get the value, ensure it's a string, and add to the map
            if (itr->value.IsString()) {
                resultTags[key] = itr->value.GetString();
            }
            else {
                // Handle non-string values (e.g., convert numbers to strings)
                // For ffprobe, tags are typically strings, but this is a fallback
                if (itr->value.IsNumber()) {
                    resultTags[key] = std::to_string(itr->value.GetDouble());
                }
                else
                {
                    auto typeName = JsonUtils::TypeToString(itr->value.GetType());
                    spdlog::error("Error: could not parse tag (GetKeyValueMap), Key name: ", key, ", type: ", typeName);
                }
                // Add more conversions if needed (e.g., bool, null)
            }
        }

        return resultTags;
    }

    std::string TypeToString(rapidjson::Type type) {
        constexpr std::array<const char*, 7> names = {
            "Null", "False", "True", "Object", "Array", "String", "Number"
        };
        return (type >= rapidjson::kNullType && type <= rapidjson::kNumberType)
            ? names[type]
            : "Unknown";
    }

    std::string valueToString(const rapidjson::Value& value) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        value.Accept(writer); // Serialize the Value into the buffer
        return buffer.GetString();
    }

    // Helper to escape JSON strings (basic version)
    std::string escapeJsonString(const std::string& input) {
        std::ostringstream oss;
        for (char c : input) {
            switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
            }
        }
        return oss.str();
    }




}




