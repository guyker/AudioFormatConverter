#pragma once

#include "MediaRecord.h"

#include <optional>
#include <string>

#include <rapidjson/document.h>

std::string SerializeMediaRecord(const MediaRecord& record);
std::optional<MediaRecord> ParseMediaRecord(
    const rapidjson::Value& value, std::string* error = nullptr);
