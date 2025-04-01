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

#include "MediaInformation.h"


namespace fs = std::filesystem;


struct MediaTrack
{
	std::filesystem::path trackPath; //media file path / location
	long long fileSize;	//file size in bytes
	MediaInformation formatInfo;	//media information / tags
	std::wstring mediaInfoString;	//media information / tags in json string


	static bool IsFileConvertable(std::filesystem::path filePath)
	{
		//L".mp3", L".wav", L".flac", L".aac", L".ogg", L".wma", L".m4a"
		static std::set<std::wstring> fileExtensionList = { L".flac", L".ape", L".dsf", L".dff", L".dsd", L".wv", L".wav", L".m2ts" , L".m4a" };
		auto fileExtension = CommonUtils::ToLower(filePath.extension().wstring());

		return filePath.has_extension() && (fileExtensionList.find(filePath.extension().wstring()) != fileExtensionList.end());
	}

	static bool IsFileAcceptedAudioFile(std::filesystem::path filePath)
	{
		static std::set<std::wstring> fileExtensionList = { L".flac", L".mp3" };
		auto fileExtension = CommonUtils::ToLower(filePath.extension().wstring());

		return filePath.has_extension() && (fileExtensionList.find(filePath.extension().wstring()) != fileExtensionList.end());
	}

	static bool IsValidMedia(const fs::directory_entry& entry)
	{
		if (entry.is_regular_file() && entry.path().has_extension())
		{
			auto fileEextension = entry.path().extension();
			if (IsFileAcceptedAudioFile(entry.path()))
			{
				return true;
			}
		}

		return false;
	}

	static bool IsValidMedia(std::filesystem::path path)
	{
		if (fs::is_regular_file(path) && path.has_extension())
		{
			auto fileEextension = path.extension();
			if (IsFileAcceptedAudioFile(path))
                #include <thread> // Add this include at the top of the file

                // Replace the problematic line with the following code
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
			{
				return true;
			}
		}

		return false;
	}

	static MediaInformation ParseMediaTrack(std::wstring jsonString);

	//create a media file (on filesystem) from a media track
	static std::filesystem::path ExtractMediaInformationFromFile(std::filesystem::path mediaFilePath, std::filesystem::path outFile)
	{
		using namespace std::string_literals;


		int status = 0;

		auto tmpPath = fs::temp_directory_path();
		//fs::path tmpFilePath{ tmpPath.generic_wstring() + L"\\media_info.json"s };
		fs::path tmpFilePath{ tmpPath / outFile };


		std::wstring cmdExecNameW{ L"ffprobe -v quiet -print_format json -show_format "s };
		std::wstring commandW{ cmdExecNameW + L"\""s + mediaFilePath.generic_wstring() + L"\""s + L" > \""s + tmpFilePath.generic_wstring() + L"\""s };

		//std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + L"'" + _targetTMPPath.generic_wstring() + L"'" };

		rapidjson::Document jsonDoc = nullptr;

		try {
			//std::wcout << L"Getting media info:: " << mediaFilePath.generic_wstring() << std::endl;

			if (fs::exists(tmpFilePath)) {
				std::error_code ec;
				if (fs::remove(tmpFilePath, ec)) {
				}
			}

			status = _wsystem(commandW.c_str());
			//  status = std::system(commandW.c_str());

			  ////std::string narrowCommand(commandW.begin(), commandW.end());
			  //status = std::system(narrowCommand.c_str());




			if (status == 0)
			{	
					//if (fs::exists(tmpFilePath)) {
					//    std::error_code ec;
					//    if (fs::remove(tmpFilePath, ec)) {
					//    }
					//}

				return tmpFilePath;
			}
			else
			{
				std::size_t hashNumber = std::hash<std::wstring>{}(mediaFilePath);
				auto tmpName = "tmp_media_" + std::to_string(hashNumber) + ".data";

				auto extension = mediaFilePath.extension();

				fs::path tmpFixFilePath{ tmpPath / tmpName };
				tmpFixFilePath.replace_extension(extension);

				try
				{
					fs::copy(mediaFilePath, tmpFixFilePath);
				}
				catch (const std::exception& ex) {
					std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << ex.what() << std::endl;

				}
				std::wstring commandWAlt{ cmdExecNameW + L"\""s + tmpFixFilePath.generic_wstring() + L"\""s + L" > \""s + tmpFilePath.generic_wstring() + L"\""s };

				status = _wsystem(commandWAlt.c_str());

				std::error_code ec;
				fs::remove(tmpFixFilePath, ec);

				if (status == 0)
				{
					return tmpFilePath;
				}
				else
				{
					int i = 0;
				}
				//  return tmpFilePath;
			}
		}
		catch (const std::exception& ex) {
			std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << ex.what() << std::endl;

		}

		return std::filesystem::path{};;
	}

	//ststic function that parses a json metadata JSON and returns an instance of MediaInformation 
	//static MediaInformation ParseMediaInformation(GenericObject::Object formatTag)
	static MediaInformation ParseMediaInformation(const Value& formatTag)
	{
		MediaInformation mi;

		if (auto filename = JsonUtils::tryParseMember<std::wstring>(formatTag, "filename")) { mi.filename = *filename; }

		if (auto nb_streams = JsonUtils::tryParseMember<int>(formatTag, "nb_streams")) { mi.nb_streams = *nb_streams; }
		if (auto nb_programs = JsonUtils::tryParseMember<int>(formatTag, "nb_programs")) { mi.nb_programs = *nb_programs; }
		if (auto nb_stream_groups = JsonUtils::tryParseMember<int>(formatTag, "nb_stream_groups")) { mi.nb_stream_groups = *nb_stream_groups; }

		if (auto format_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_name")) { mi.format_name = *format_name; }
		if (auto format_long_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_long_name")) { mi.format_long_name = *format_long_name; }
		if (auto codec_type = JsonUtils::tryParseMember<std::string>(formatTag, "codec_type")) { mi.codec_type = *codec_type; }
		if (auto start_time = JsonUtils::tryParseMember<std::string>(formatTag, "start_time")) { mi.start_time = *start_time; }
		if (auto size = JsonUtils::tryParseMember<std::string>(formatTag, "size")) { mi.size = *size; }
		if (auto bit_rate = JsonUtils::tryParseMember<std::string>(formatTag, "bit_rate")) { mi.bit_rate = *bit_rate; }

		if (auto duration = JsonUtils::tryParseMember<long>(formatTag, "duration")) { mi.duration = *duration; }
		if (auto probe_score = JsonUtils::tryParseMember<int>(formatTag, "probe_score")) { mi.probe_score = *probe_score; }


		if (formatTag.FindMember("tags") != formatTag.MemberEnd())
		{
			auto tags = formatTag["tags"].GetObj();


			if (auto album = JsonUtils::tryParseMember<std::wstring>(tags, "album")) { mi.tags.album = *album; }
			if (auto disc = JsonUtils::tryParseMember<std::wstring>(tags, "disc")) { mi.tags.disc = *disc; }
			if (auto album_dynamic_range = JsonUtils::tryParseMember<std::wstring>(tags, "album_dynamic_range")) { mi.tags.album_dynamic_range = *album_dynamic_range; }
			if (auto dynamic_range = JsonUtils::tryParseMember<std::wstring>(tags, "dynamic_range")) { mi.tags.dynamic_range = *dynamic_range; }
			if (auto artist = JsonUtils::tryParseMember<std::wstring>(tags, "artist")) { mi.tags.artist = *artist; }
			if (auto album_artist = JsonUtils::tryParseMember<std::wstring>(tags, "album_artist")) { mi.tags.album_artist = *album_artist; }
			if (auto composer = JsonUtils::tryParseMember<std::wstring>(tags, "composer")) { mi.tags.composer = *composer; }
			if (auto copyright = JsonUtils::tryParseMember<std::wstring>(tags, "copyright")) { mi.tags.copyright = *copyright; }
			if (auto label = JsonUtils::tryParseMember<std::wstring>(tags, "label")) { mi.tags.label = *label; }
			if (auto year = JsonUtils::tryParseMember<std::wstring>(tags, "year")) { mi.tags.year = *year; }
			if (auto comment = JsonUtils::tryParseMember<std::wstring>(tags, "comment")) { mi.tags.comment = *comment; }
			if (auto genre = JsonUtils::tryParseMember<std::wstring>(tags, "genre")) { mi.tags.genre = *genre; }
			if (auto publisher = JsonUtils::tryParseMember<std::wstring>(tags, "publisher")) { mi.tags.publisher = *publisher; }
			if (auto title = JsonUtils::tryParseMember<std::wstring>(tags, "title")) { mi.tags.title = *title; }
			if (auto track = JsonUtils::tryParseMember<std::wstring>(tags, "track")) { mi.tags.track = *track; }
			if (auto track_total = JsonUtils::tryParseMember<std::wstring>(tags, "track_total")) { mi.tags.track_total = *track_total; }
			if (auto date = JsonUtils::tryParseMember<std::wstring>(tags, "date")) { mi.tags.date = *date; }
			if (auto encoder = JsonUtils::tryParseMember<std::wstring>(tags, "encoder")) { mi.tags.encoder = *encoder; }
			if (auto encoded_by = JsonUtils::tryParseMember<std::wstring>(tags, "encoded_by")) { mi.tags.encoded_by = *encoded_by; }
			if (auto organization = JsonUtils::tryParseMember<std::wstring>(tags, "organization")) { mi.tags.organization = *organization; }
		}

		return mi;
	}

};


