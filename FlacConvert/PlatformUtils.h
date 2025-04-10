#pragma once

#include <string>
#include <stdexcept>
#include <filesystem>



namespace PlatformUtils {
	void OpenDirectoryInExplorer(std::wstring dirName);
	void OpenDirectory(const std::filesystem::path& path);

	void waitForKeyPress();
}