#pragma once

#include <string>
#include <filesystem>
#include <vector>


class FolderCompare
{

public:

	std::vector<std::wstring> GetFolderNamesList(std::filesystem::path path);

	std::vector<std::wstring> Compare(std::filesystem::path pathA, std::filesystem::path pathB);


};

