#pragma once



#include "rapidjson/rapidjson.h" 
#include "rapidjson/document.h" 



struct MediaInformation
{
	std::string FilePathKey;



	std::string filename;
	std::string nb_streams;
	std::string nb_programs;
	std::string format_name;
	std::string format_long_name;
	std::string start_time;
	long duration;
	std::string size;
	std::string bit_rate;
	std::string probe_score;

	struct _tags
	{
		std::string album;
		std::string artist;
		std::string album_artist;
		std::string comment;
		std::string genre;
		std::string publisher;
		std::string title;
		std::string track;
		std::string date;

	} tags;
	//rapidjson::Document JSONDoc;
};