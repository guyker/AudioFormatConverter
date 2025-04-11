#pragma once

#include <string>
#include <stdexcept>
#include <filesystem>



namespace PlatformUtils {
	std::string WideToUTF8(const std::wstring& wstr);
	std::string wstringToUtf8_ver2(const std::wstring& wstr);

	void OpenDirectoryInExplorer(std::wstring dirName);
	void OpenDirectory(const std::filesystem::path& path);

	void waitForKeyPress();
}