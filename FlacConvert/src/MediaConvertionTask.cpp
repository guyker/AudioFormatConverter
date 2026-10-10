
#include "MediaConvertionTask.h"

#include <algorithm>
#include <string>
#include <cerrno>
#include <charconv>
#include <filesystem>
#include <optional>
#include <sstream>
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
#include "FFmpeg.h"
#include "PlatformUtils.h"

namespace fs = std::filesystem;

namespace
{
std::optional<int> parseInteger(const std::optional<std::string>& value)
{
    if (!value || value->empty()) {
        return std::nullopt;
    }

    int parsed = 0;
    const auto result = std::from_chars(value->data(), value->data() + value->size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value->data() + value->size()) {
        return std::nullopt;
    }
    return parsed;
}

bool isAtLeastCDQuality(const fs::path& source)
{
    const auto metadata = FFmpeg::GetFFprobeMetadataAPI(source);
    const auto audioStream = std::find_if(metadata.streams.begin(), metadata.streams.end(),
        [](const Stream& stream) {
            return stream.codec_type && *stream.codec_type == "audio";
        });
    if (audioStream == metadata.streams.end()) {
        spdlog::error("Cannot convert '{}': no readable audio stream was found.",
            CommonUtils::path_to_utf8(source));
        return false;
    }

    const auto sampleRate = parseInteger(audioStream->sample_rate);
    const auto bitDepth = audioStream->bits_per_sample;
    std::vector<std::string> issues;
    if (!sampleRate) {
        issues.emplace_back("sample rate could not be verified");
    }
    else if (*sampleRate < 44100) {
        issues.emplace_back("sample rate " + std::to_string(*sampleRate) + " Hz is below 44100 Hz");
    }
    if (!bitDepth) {
        issues.emplace_back("bit depth could not be verified");
    }
    else if (*bitDepth < 16) {
        issues.emplace_back("bit depth " + std::to_string(*bitDepth) + " is below 16-bit");
    }

    if (issues.empty()) {
        return true;
    }

    std::ostringstream reason;
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (i > 0) {
            reason << "; ";
        }
        reason << issues[i];
    }
    spdlog::error("Rejecting source '{}': {}. Conversion requires at least 16-bit audio at 44100 Hz.",
        CommonUtils::path_to_utf8(source), reason.str());
    return false;
}

#ifdef _WIN32
std::wstring quoteWindowsArgument(const std::wstring& argument)
{
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        quoted.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        quoted += character;
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}
#endif

int runFFmpeg(const std::wstring& executable, const fs::path& source, const fs::path& destination, bool overwrite)
{
#ifdef _WIN32
    const std::wstring overwriteOption = overwrite ? L"-y" : L"-n";
    const std::vector<std::wstring> arguments{
        executable, L"-i", PlatformUtils::WindowsLongPath(source), L"-map", L"0:a:0",
        L"-af", L"aresample=osf=s16:osr=44100:dither_method=triangular", L"-c:a", L"flac",
        overwriteOption, L"-v", L"warning", L"-stats",
        PlatformUtils::WindowsLongPath(destination)
    };
    // The Windows CRT joins spawn arguments into a command line without quoting them.
    std::vector<std::wstring> quotedArguments;
    quotedArguments.reserve(arguments.size());
    for (const auto& argument : arguments) {
        quotedArguments.push_back(quoteWindowsArgument(argument));
    }
    std::vector<const wchar_t*> pointers;
    pointers.reserve(quotedArguments.size() + 1);
    for (const auto& argument : quotedArguments) {
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
        executableUtf8, "-i", source.string(), "-map", "0:a:0",
        "-af", "aresample=osf=s16:osr=44100:dither_method=triangular", "-c:a", "flac",
        overwrite ? "-y" : "-n", "-v", "warning", "-stats", destination.string()
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
    if (!isAtLeastCDQuality(_sourcePath)) {
        _status = -1;
        return _status;
    }

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
