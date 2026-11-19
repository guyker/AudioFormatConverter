#pragma once

#include <string>
#include <stdexcept>
#include <filesystem>
#include <mutex>
#include <vector>



namespace PlatformUtils {
	class ScopedCurrentDirectory
	{
	public:
		ScopedCurrentDirectory();
		~ScopedCurrentDirectory();
		ScopedCurrentDirectory(const ScopedCurrentDirectory&) = delete;
		ScopedCurrentDirectory& operator=(const ScopedCurrentDirectory&) = delete;

		bool EnterValidatedDirectory(const std::filesystem::path& target,
			const std::filesystem::path& sourceRoot, bool createDirectories,
			bool* createdTargetDirectory = nullptr);
		std::filesystem::path ResolvePath(const std::filesystem::path& path) const;
		bool IsAnchored(const std::filesystem::path& target) const;
		bool Restore();

	private:
		std::unique_lock<std::recursive_mutex> _lock;
		std::filesystem::path _previousPath;
		std::filesystem::path _targetPath;
		std::filesystem::path _canonicalTarget;
#ifndef _WIN32
		int _previousDirectory{-1};
#else
		std::vector<void*> _lockedDirectories;
#endif
		bool _saved{false};
		bool _active{false};
		bool _longPathFallback{false};
	};

	std::string WideToUTF8(const std::wstring& wstr);
	std::string wstringToUtf8_ver2(const std::wstring& wstr);
#ifdef _WIN32
	std::wstring WindowsLongPath(const std::filesystem::path& path, bool forceExtended = false);
#endif

	void OpenDirectoryInExplorer(std::wstring dirName);
	void OpenDirectory(const std::filesystem::path& path);

	// True when stdout is an interactive terminal that accepts ANSI colour (honours NO_COLOR).
	bool EnableConsoleColors();

	int waitForKeyPress(std::string message);
}