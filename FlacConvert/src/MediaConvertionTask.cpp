
#include "MediaConvertionTask.h"

#include <string>
#include <cerrno>
#include <filesystem>
#include <utility>
#include <vector>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include "AppSettings.h"
#include "CommonUtils.h"
#include "PlatformUtils.h"

namespace fs = std::filesystem;

namespace
{
int runFFmpeg(const std::wstring& executable, const fs::path& source, const fs::path& destination, bool overwrite)
{
#ifdef _WIN32
    const std::wstring overwriteOption = overwrite ? L"-y" : L"-n";
    const std::vector<std::wstring> arguments{
        executable, L"-i", PlatformUtils::WindowsLongPath(source), L"-c:v", L"copy", L"-sample_fmt", L"s16",
        L"-ar", L"44100", overwriteOption, L"-v", L"warning", L"-stats",
        PlatformUtils::WindowsLongPath(destination)
    };
    std::vector<const wchar_t*> pointers;
    pointers.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        pointers.push_back(argument.c_str());
    }
    pointers.push_back(nullptr);
    const intptr_t result = _wspawnvp(_P_WAIT, executable.c_str(), pointers.data());
    if (result == -1) {
        spdlog::error("Failed to launch FFmpeg: {}", std::system_category().message(errno));
        return -1;
    }
    return static_cast<int>(result);
#else
    const std::string executableUtf8 = PlatformUtils::wstringToUtf8_ver2(executable);
    const std::vector<std::string> arguments{
        executableUtf8, "-i", source.string(), "-c:v", "copy", "-sample_fmt", "s16",
        "-ar", "44100", overwrite ? "-y" : "-n", "-v", "warning", "-stats", destination.string()
    };
    std::vector<char*> pointers;
    pointers.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        pointers.push_back(const_cast<char*>(argument.c_str()));
    }
    pointers.push_back(nullptr);

    const pid_t child = fork();
    if (child == -1) {
        spdlog::error("Failed to launch FFmpeg: {}", std::system_category().message(errno));
        return -1;
    }
    if (child == 0) {
        execvp(executableUtf8.c_str(), pointers.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            spdlog::error("Failed while waiting for FFmpeg: {}", std::system_category().message(errno));
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
}
}


int MediaConvertionTask::ConvertFile()
{
    std::error_code ec;
    const bool destinationExists = fs::exists(_targetPath, ec);
    if (ec) {
        spdlog::error("Cannot inspect destination '{}': {}",
            CommonUtils::utf8string_to_string(_targetPath.u8string()), ec.message());
        _status = -1;
        return _status;
    }
    if (!_overwriteTarget && destinationExists) {
        spdlog::error("Destination already exists: {}", CommonUtils::utf8string_to_string(_targetPath.u8string()));
        _status = -1;
        return _status;
    }

    std::wstring executable{ L"ffmpeg" };
    const auto settings = AppSettingsJson::AppSetting();
    if (settings && !settings->FLACSettings.ffmpeg_exe_name.empty()) {
        executable = settings->FLACSettings.ffmpeg_exe_name;
    }

    spdlog::info("Processing: {}", CommonUtils::utf8string_to_string(_sourcePath.u8string()));
    const int result = runFFmpeg(executable, _sourcePath, _targetTMPPath, _overwriteTarget);
    if (result != 0) {
        if (result != -1) {
            spdlog::error("FFmpeg failed with exit code {} for '{}'",
                result, CommonUtils::utf8string_to_string(_sourcePath.u8string()));
        }
        fs::remove(_targetTMPPath, ec);
        _status = -1;
        return _status;
    }

    _status = 0;
    return _status;
}


int MediaConvertionTask::Run() {
    if (_status != -1) {
        _status = ConvertFile();
    }

    return _status;
}


int MediaConvertionTask::PostRun() {
    if (_status != -1) {
        _status = InstallTemporaryOutput();
    }

    return _status;
}

int MediaConvertionTask::InstallTemporaryOutput()
{
    if (_status == -1) {
        return _status;
    }

    std::error_code ec;
    if (!fs::is_regular_file(_targetTMPPath, ec) || ec) {
        spdlog::error("Converted temporary output is missing: {}", CommonUtils::utf8string_to_string(_targetTMPPath.u8string()));
        _status = -1;
        return _status;
    }

    bool installed = false;
#ifdef _WIN32
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (_overwriteTarget ? MOVEFILE_REPLACE_EXISTING : 0);
    installed = MoveFileExW(_targetTMPPath.c_str(), _targetPath.c_str(), flags) != 0;
    const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
#else
    if (_overwriteTarget) {
        fs::rename(_targetTMPPath, _targetPath, ec);
        installed = !ec;
    }
    else {
        fs::create_hard_link(_targetTMPPath, _targetPath, ec);
        installed = !ec;
        std::error_code error = ec;
        if (!installed && ec != std::errc::file_exists) {
            std::error_code copyError;
            installed = fs::copy_file(
                _targetTMPPath, _targetPath, fs::copy_options::none, copyError);
            if (!installed) {
                error = copyError ? copyError : error;
            }
        }
        if (installed) {
            std::error_code cleanupError;
            fs::remove(_targetTMPPath, cleanupError);
            if (cleanupError) {
                spdlog::warn("Installed output, but could not remove temporary link '{}': {}",
                    CommonUtils::path_to_utf8(_targetTMPPath), cleanupError.message());
            }
        }
        ec = installed ? std::error_code{} : error;
    }
    const std::error_code error = ec;
#endif
    if (!installed) {
        spdlog::error("Failed to install converted output '{}': {}",
            CommonUtils::utf8string_to_string(_targetPath.u8string()), error.message());
        _status = -1;
        return _status;
    }

    _status = 0;
    return _status;
}
