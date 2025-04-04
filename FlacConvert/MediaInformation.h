#pragma once

#include <set>
#include <string>
#include <filesystem>
#include <vector>
#include <tuple>
#include <vector>
#include <optional>
#include <cstdint>
#include <map>

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

/// <summary>
/// 
/// 
/// </summary>
/// 

	// Represents dynamic metadata tags (key-value pairs)
struct Tags {
	std::map<std::string, std::string> tags; // Key-value map, keys lowercase (e.g., "artist"), values strings (e.g., "The Beatles"), from file metadata
};

// Represents the "disposition" object for streams/frames
struct Disposition {
	int default_stream{ 0 }; // Default stream flag, 1 = yes, 0 = no
	int dub{ 0 }; // Dubbed stream flag, 1 = yes, 0 = no
	int original{ 0 }; // Original language flag, 1 = yes, 0 = no
	int comment{ 0 }; // Commentary flag, 1 = yes, 0 = no
	int lyrics{ 0 }; // Lyrics flag, 1 = yes, 0 = no
	int karaoke{ 0 }; // Karaoke flag, 1 = yes, 0 = no
	int forced{ 0 }; // Forced (e.g., subtitles) flag, 1 = yes, 0 = no
	int hearing_impaired{ 0 }; // Hearing impaired flag, 1 = yes, 0 = no
	int visual_impaired{ 0 }; // Visually impaired flag, 1 = yes, 0 = no
	int clean_effects{ 0 }; // Clean effects flag, 1 = yes, 0 = no
	int attached_pic{ 0 }; // Attached picture flag, 1 = yes, 0 = no
	int timed_thumbnails{ 0 }; // Timed thumbnails flag, 1 = yes, 0 = no
};

// Represents the "format" section (-show_format)
struct Format {
	std::string filename; // File path/name, e.g., "/path/to/song.flac"
	int nb_streams{ 0 }; // Number of streams, non-negative integer, e.g., 1
	int nb_programs{ 0 }; // Number of programs, non-negative integer, e.g., 0
//		int nb_stream_groups;
	std::string format_name; // Container format short name, e.g., "flac", "mp3"
	std::string format_long_name; // Container format descriptive name, e.g., "raw FLAC"
	std::optional<std::string> start_time; // Start time in seconds, string with decimal, e.g., "0.000000", optional
	std::optional<std::string> duration; // Duration in seconds, string with decimal, e.g., "123.456789", optional
	std::optional<std::string> size; // File size in bytes, string with integer, e.g., "12345678", optional
	std::optional<std::string> bit_rate; // Overall bitrate in bits/s, string with integer, e.g., "960000", optional
	int probe_score{ 0 }; // Format detection confidence, integer 0-100

	std::optional<Tags> tags; // Container metadata tags, e.g., "artist": "The Beatles", optional
};


struct MediaInformation
{
	Format format2;

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



