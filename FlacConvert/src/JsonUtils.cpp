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



    const std::string* getValueByKey(const std::map<std::string, std::string, CaseInsensitiveCompare>& map, const std::string& key) {
        auto it = map.find(key);
        return it != map.end() ? &it->second : nullptr;
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




