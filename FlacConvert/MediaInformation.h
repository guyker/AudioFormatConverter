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

//struct MediaTrack;


	// Represents dynamic metadata tags (key-value pairs)
//struct Tags {
//	std::map<std::string, std::string> tags; // Key-value map, keys lowercase (e.g., "artist"), values strings (e.g., "The Beatles"), from file metadata
//};

using Tags = std::map<std::string, std::string>;

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
	std::wstring filename; // File path/name, e.g., "/path/to/song.flac"
    
    std::optional<uint64_t> file_size;

	int nb_streams{ 0 }; // Number of streams, non-negative integer, e.g., 1
	int nb_programs{ 0 }; // Number of programs, non-negative integer, e.g., 0
	int nb_stream_groups; //OPTIONAL ????

	std::string format_name; // Container format short name, e.g., "flac", "mp3"
	std::string format_long_name; // Container format descriptive name, e.g., "raw FLAC"
	std::optional<int64_t> start_time; // Start time in seconds, string with decimal, e.g., "0.000000", optional
	std::optional<double> duration; // Duration in seconds, string with decimal, e.g., "123.456789", optional
	std::optional<std::string> size; // File size in bytes, string with integer, e.g., "12345678", optional
	//std::optional<std::string> bit_rate; // Overall bitrate in bits/s, string with integer, e.g., "960000", optional
	std::optional<long long> bit_rate; // Overall bitrate in bits/s, string with integer, e.g., "960000", optional
	int probe_score{ 0 }; // Format detection confidence, integer 0-100

	std::optional<Tags> tags; // Container metadata tags, e.g., "artist": "The Beatles", optional
};

// Represents an entry in the "streams" array (-show_streams)
struct Stream {
    std::optional<int> frame_size; // Added to fix the error
    std::optional<int64_t> nb_frames;

    int index{ 0 }; // Stream index, non-negative integer, e.g., 0
    std::optional<std::string> codec_name; // Codec name, e.g., "flac", "mp3", optional
    std::optional<std::string> codec_long_name; // Codec descriptive name, e.g., "FLAC (Free Lossless Audio Codec)", optional
    std::optional<std::string> codec_type; // Stream type, e.g., "audio", "video", optional
    std::optional<std::string> codec_time_base; // Codec time base, e.g., "1/44100", optional
    std::optional<std::string> codec_tag_string; // Human-readable codec tag, e.g., "[0][0][0][0]", optional
    std::optional<std::string> codec_tag; // Hex codec tag, e.g., "0x0000", optional
    std::optional<std::string> sample_fmt; // Audio sample format, e.g., "s16", "fltp", optional
    std::optional<std::string> sample_rate; // Sample rate in Hz, e.g., "44100", optional
    std::optional<int> channels; // Number of channels, positive integer, e.g., 2, optional
    std::optional<std::string> channel_layout; // Channel layout, e.g., "stereo", optional
    std::optional<int> bits_per_sample; // Bits per sample, e.g., 16, 24, optional
    std::optional<std::string> r_frame_rate; // Rational frame rate, e.g., "0/0" (audio), "25/1" (video), optional
    std::optional<std::string> avg_frame_rate; // Average frame rate, e.g., "0/0" (audio), optional
    std::optional<std::string> time_base; // Stream time base, e.g., "1/44100", optional
    std::optional<int64_t> start_pts; // First frame PTS, integer, e.g., 0, optional
    std::optional<int64_t> start_time; // Start time in seconds, e.g., "0.000000", optional
    std::optional<int64_t> duration_ts; // Duration in time base units, integer, e.g., 5432100, optional
    std::optional<std::string> duration; // Duration in seconds, e.g., "123.456789", optional
    std::optional<long long> bit_rate; // Stream bitrate in bits/s, e.g., "960000", optional
    std::optional<Disposition> disposition; // Stream disposition flags, optional
    std::optional<Tags> tags; // Stream-specific tags, e.g., "language": "eng", optional
};

// Represents an entry in the "packets" array (-show_packets)
struct Packet {
    std::optional<std::string> codec_type; // Packet type, e.g., "audio", optional
    std::optional<int> stream_index; // Stream index, non-negative integer, e.g., 0, optional
    std::optional<int64_t> pts; // Presentation timestamp, integer, e.g., 123456, optional
    std::optional<std::string> pts_time; // PTS in seconds, e.g., "0.002789", optional
    std::optional<int64_t> dts; // Decoding timestamp, integer, e.g., 123456, optional
    std::optional<std::string> dts_time; // DTS in seconds, e.g., "0.002789", optional
    std::optional<std::string> duration; // Duration in seconds, e.g., "0.023219", optional
    std::optional<int64_t> duration_ts; // Duration in time base units, integer, e.g., 1024, optional
    std::optional<std::string> convergence_window; // Convergence window, rarely used, optional
    std::optional<int64_t> size; // Packet size in bytes, integer, e.g., 512, optional
    std::optional<int64_t> pos; // Position in file in bytes, integer, e.g., 1024, optional
    std::optional<std::string> flags; // Packet flags, e.g., "K" (keyframe), optional
};

// Represents an entry in the "frames" array (-show_frames)
struct Frame {
    std::optional<std::string> media_type; // Frame type, e.g., "audio", optional
    std::optional<int> stream_index; // Stream index, non-negative integer, e.g., 0, optional
    std::optional<int> key_frame; // Keyframe flag, 1 = yes, 0 = no, optional
    std::optional<int64_t> pts; // Presentation timestamp, integer, e.g., 123456, optional
    std::optional<std::string> pts_time; // PTS in seconds, e.g., "0.002789", optional
    std::optional<int64_t> pkt_dts; // Packet DTS, integer, e.g., 123456, optional
    std::optional<std::string> pkt_dts_time; // Packet DTS in seconds, e.g., "0.002789", optional
    std::optional<std::string> duration; // Duration in seconds, e.g., "0.023219", optional
    std::optional<int64_t> duration_ts; // Duration in time base units, integer, e.g., 1024, optional
    std::optional<std::string> sample_fmt; // Audio sample format, e.g., "s16", optional
    std::optional<int> channels; // Number of channels, positive integer, e.g., 2, optional
    std::optional<std::string> channel_layout; // Channel layout, e.g., "stereo", optional
    std::optional<int> sample_rate; // Sample rate in Hz, integer, e.g., 44100, optional
};

// Represents an entry in the "programs" array (-show_programs)
struct Program {
    int program_id{ 0 }; // Program ID, integer, e.g., 1
    int program_num{ 0 }; // Program number, integer, e.g., 1
    std::vector<int> stream_indices; // Indices of streams in this program, e.g., {0}
    std::optional<Tags> tags; // Program-specific tags, optional
};

// Represents an entry in the "chapters" array (-show_chapters)
struct Chapter {
    int id{ 0 }; // Chapter ID, integer, e.g., 0
    std::optional<std::string> time_base; // Time base, e.g., "1/1000", optional
    std::optional<int64_t> start; // Start time in time base units, integer, e.g., 0, optional
    std::optional<std::string> start_time; // Start time in seconds, e.g., "0.000000", optional
    std::optional<int64_t> end; // End time in time base units, integer, e.g., 123456, optional
    std::optional<std::string> end_time; // End time in seconds, e.g., "123.456789", optional
    std::optional<Tags> tags; // Chapter-specific tags, e.g., "title": "Intro", optional
};

// Represents the "error" section (-show_error)
struct Error {
    int code{ 0 }; // Error code, integer, e.g., -1
    std::string string; // Error message, e.g., "Invalid data found"
};

// Represents an entry in the "library_versions" object (-show_versions)
struct LibraryVersion {
    std::string name; // Library name, e.g., "libavcodec"
    int major{ 0 }; // Major version, integer, e.g., 58
    int minor{ 0 }; // Minor version, integer, e.g., 134
    int micro{ 0 }; // Micro version, integer, e.g., 100
    std::optional<std::string> version; // Full version string, e.g., "58.134.100", optional
};

// Represents an entry in the "pixel_formats" array (-show_pixel_formats)
struct PixelFormat {
    std::string name; // Pixel format name, e.g., "yuv420p"
    int nb_components{ 0 }; // Number of components, integer, e.g., 3
    int bits_per_pixel{ 0 }; // Bits per pixel, integer, e.g., 12
};

// Main structure for full ffprobe JSON output
struct FFprobeOutput {

	Format format; // Format section from -show_format, optional
	std::vector<Stream> streams; // Streams array from -show_streams

/*
    std::optional<Format> format; // Format section from -show_format, optional
    std::vector<Stream> streams; // Streams array from -show_streams
    std::vector<Packet> packets; // Packets array from -show_packets
    std::vector<Frame> frames; // Frames array from -show_frames
    std::vector<Program> programs; // Programs array from -show_programs
    std::vector<Chapter> chapters; // Chapters array from -show_chapters
    std::optional<Error> error; // Error section from -show_error, optional
    std::map<std::string, LibraryVersion> library_versions; // Library versions from -show_versions, key is library name
    std::vector<PixelFormat> pixel_formats; // Pixel formats array from -show_pixel_formats
*/

	//for debuging / easy access
	struct format_tags_debug
	{
		//for easy access and debugging
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
	} format_tags;
};






