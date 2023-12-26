#pragma once



#include "rapidjson/rapidjson.h" 
#include "rapidjson/document.h" 



struct MediaInformation
{
	std::wstring FilePath;
	long long FileSize;


	std::wstring filename;
	std::wstring nb_streams;
	std::wstring nb_programs;
	std::wstring format_name;
	std::wstring format_long_name;
	std::wstring start_time;
	std::wstring duration;
	std::wstring size;
	std::wstring bit_rate;
	std::wstring probe_score;


	rapidjson::Document JSONDoc;
};