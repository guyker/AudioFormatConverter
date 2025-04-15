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
	uintmax_t fs_fileSize{ 0 };	//file size in bytes
	FFprobeOutput formatInfo;	//media information / tags
	std::wstring mediaInfoString;	//media information / tags in json string

	//keep it last for agregation
	std::optional<std::string> LastErroString;


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
              //  #include <thread> // Add this include at the top of the file

                // Replace the problematic line with the following code
              //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
			{
				return true;
			}
		}

		return false;
	}

	//returns media information (json string and media objec) from a media file (on file system)
	static std::tuple<FFprobeOutput, std::wstring> ReadMediaInfoFromFile(std::filesystem::path mediaFilePath);

	//parse media information from a json string
	static FFprobeOutput ParseFFprobeInformation(const Value& formatTag);
	static FFprobeOutput ParseFFprobeInformation(std::wstring jsonString);
};


