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



struct Stream {
	int index;
	std::string codec_name;
	std::string codec_long_name;
	std::string codec_type;
	std::string codec_tag_string;
	std::string codec_tag;
	std::string sample_fmt;
	std::string sample_rate;
	int channels;
	std::string channel_layout;
	int bits_per_sample;
	int initial_padding;
	std::string r_frame_rate;
	std::string avg_frame_rate;
	std::string time_base;
	int64_t start_pts;
	std::string start_time;
	int64_t duration_ts;
	std::string duration;
	std::string bit_rate;
//	Disposition disposition;
//	std::map<std::string, std::string> tags; // "tags" can contain various key-value pairs
};

struct MediaInformation
{
	struct format_t
	{
		std::string codec_type; //XXXXXX

		std::wstring filename;
		int nb_streams;
		int nb_programs;
		int nb_stream_groups;

		std::string format_name;
		std::string format_long_name;
		std::string start_time;
		long duration;
		std::string size;
		std::string bit_rate;

		int probe_score;

		struct tags_t
		{
			std::wstring album;
			std::wstring artist;
			std::wstring album_artist;
			std::wstring genre;
			std::wstring disc;
			std::wstring title;
			std::wstring track;
			std::wstring track_total;
			std::wstring date;
			std::wstring comment;
			std::wstring publisher;
			std::wstring encoder;
			std::wstring encoded_by;
			std::wstring organization;
			std::wstring composer;
			std::wstring copyright;

			std::wstring album_dynamic_range;
			std::wstring dynamic_range;

			std::wstring label;
			std::wstring year;

			std::wstring MusicBrainz_Album_Release_Country; //"MusicBrainz Album Release Country"
		} tags;
	} format;

	std::vector<Stream> streams;
};



