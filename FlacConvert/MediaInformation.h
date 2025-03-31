#pragma once

#include <set>
#include <string>
#include <filesystem>
#include <vector>
#include <tuple>

#include "JsonUtils.h"
#include "CommonUtils.h"

#include <locale>
#include <codecvt>
#include <windows.h>
#include "AppSettings.h"

#include "rapidjson/rapidjson.h" 
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/ostreamwrapper.h"
#include <future>


namespace fs = std::filesystem;

struct MediaTrack;


struct MediaInformation
{
	std::wstring filename;
	int nb_streams;
	int nb_programs;
	int nb_stream_groups;

	std::string codec_name;
	std::string codec_long_name;
	std::string codec_type;
	std::string start_time;
	long duration;
	std::string size;
	std::string bit_rate;

	int probe_score;

	struct tags_t
	{
		std::wstring album;
		std::wstring disc;
		std::wstring album_dynamic_range;
		std::wstring dynamic_range;
		std::wstring artist;
		std::wstring album_artist;
		std::wstring composer;
		std::wstring copyright;

		std::wstring label;
		std::wstring year;

		std::wstring comment;
		std::wstring genre;
		std::wstring publisher;
		std::wstring title;
		std::wstring track;
		std::wstring track_total;
		std::wstring date;
		std::wstring encoder;
		std::wstring encoded_by;
		std::wstring organization;

	} tags;
};



