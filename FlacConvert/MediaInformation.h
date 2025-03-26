#pragma once


struct MediaInformation
{
	std::string filename;
	int nb_streams;
	int nb_programs;
	int nb_stream_groups;

	std::string format_name;
	std::string format_long_name;
	std::string start_time;
	std::string size;
	std::string bit_rate;

	long duration;
	int probe_score;

	struct tags_t
	{
		std::string album;
		std::string album_dynamic_range;
		std::string dynamic_range;
		std::string artist;
		std::string album_artist;
		std::string composer;
		std::string copyright;

		std::string label;
		std::string year;

		std::string comment;
		std::string genre;
		std::string publisher;
		std::string title;
		std::string track;
		std::string track_total;
		std::string date;
		std::string encoder;
		std::string encoded_by;
		std::string organization;

	} tags;
};