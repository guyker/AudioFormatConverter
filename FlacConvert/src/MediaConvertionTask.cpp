
#include "MediaConvertionTask.h"

#include <algorithm>
#include <string>
#include <cerrno>
#include <charconv>
#include <cmath>
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
#include "FlacMetadata.h"

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

bool isAtLeastCDQuality(const fs::path& source, const FFprobeOutput& metadata)
{
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

fs::path sidecarPath(fs::path audio)
{
    audio += ".flac-metadata";
    return audio;
}

fs::path temporaryInputPath(fs::path audio)
{
    audio += ".input.flac";
    return audio;
}

bool installFile(const fs::path& temporary, const fs::path& target, bool overwrite)
{
    std::error_code ec;
#ifdef _WIN32
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
    if (MoveFileExW(PlatformUtils::WindowsLongPath(temporary).c_str(),
        PlatformUtils::WindowsLongPath(target).c_str(), flags)) {
        return true;
    }
    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    if (overwrite) {
        fs::rename(temporary, target, ec);
        if (!ec) {
            return true;
        }
    }
    else {
        fs::create_hard_link(temporary, target, ec);
        if (ec && ec != std::errc::file_exists) {
            std::error_code copyError;
            if (fs::copy_file(temporary, target, fs::copy_options::none, copyError)) {
                ec.clear();
            }
            else if (copyError) {
                ec = copyError;
            }
        }
        if (!ec) {
            fs::remove(temporary, ec);
            if (ec) {
                spdlog::warn("Installed output but cannot remove temporary '{}': {}",
                    CommonUtils::path_to_utf8(temporary), ec.message());
            }
            return true;
        }
    }
#endif
    spdlog::error("Cannot install '{}': {}", CommonUtils::path_to_utf8(target), ec.message());
    return false;
}

void removeTemporary(const fs::path& path)
{
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        spdlog::warn("Cannot remove temporary '{}': {}", CommonUtils::path_to_utf8(path), ec.message());
    }
}

std::vector<std::string> conversionArguments(const ConversionOptions& options,
    const FFprobeOutput& metadata)
{
    std::vector<std::string> arguments{ "-map", "0:a:0" };
    if (options.target == AudioTarget::Flac) {
        arguments.insert(arguments.end(), {
            "-af", "aresample=osf=s16:osr=44100:dither_method=triangular", "-c:a", "flac" });
        return arguments;
    }
    for (const auto& stream : metadata.streams) {
        if (!stream.disposition || !stream.disposition->attached_pic) {
            continue;
        }
        const auto& codec = stream.codec_name;
        if (codec && (*codec == "mjpeg" || *codec == "png" || *codec == "gif" ||
            *codec == "tiff" || *codec == "bmp" || *codec == "webp" || *codec == "jpegxl")) {
            arguments.insert(arguments.end(), { "-map", "0:" + std::to_string(stream.index) });
        }
        else {
            spdlog::warn("Picture stream {} cannot be embedded in MP3; retained in FLAC metadata sidecar.",
                stream.index);
        }
    }
    const auto audio = std::find_if(metadata.streams.begin(), metadata.streams.end(),
        [](const Stream& stream) { return stream.codec_type && *stream.codec_type == "audio"; });
    const auto rate = parseInteger(audio->sample_rate);
    if (*rate != 44100 && *rate != 48000) {
        arguments.insert(arguments.end(), { "-ar", "48000" });
        spdlog::info("MP3 sample rate: {} Hz -> 48000 Hz.", *rate);
    }
    if (*audio->channels > 2) {
        arguments.insert(arguments.end(), { "-ac", "2" });
        spdlog::warn("Explicitly downmixing {} channels to stereo for MP3.", *audio->channels);
    }
    arguments.insert(arguments.end(), { "-map_metadata", "0", "-map_chapters", "0",
        "-c:a", "libmp3lame", "-q:a", std::to_string(options.mp3Quality),
        "-c:v", "copy", "-id3v2_version", "4", "-write_id3v1", "0", "-write_xing", "1" });
    return arguments;
}

int runFFmpeg(const std::wstring& executable, const fs::path& source, const fs::path& destination,
    bool overwrite, const std::vector<std::string>& conversion)
{
#ifdef _WIN32
    std::vector<std::wstring> arguments{ executable, L"-nostdin", L"-xerror", L"-i",
        PlatformUtils::WindowsLongPath(source) };
    for (const auto& argument : conversion) {
        arguments.push_back(CommonUtils::utf8ToWstring(argument));
    }
    arguments.insert(arguments.end(), { overwrite ? L"-y" : L"-n", L"-v", L"warning",
        L"-stats", PlatformUtils::WindowsLongPath(destination) });
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
    std::vector<std::string> arguments{ executableUtf8, "-nostdin", "-xerror", "-i", source.string() };
    arguments.insert(arguments.end(), conversion.begin(), conversion.end());
    arguments.insert(arguments.end(), {
        overwrite ? "-y" : "-n", "-v", "warning", "-stats", destination.string() });
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
    const auto metadata = FFmpeg::GetFFprobeMetadataAPI(_sourcePath);
    if (!isAtLeastCDQuality(_sourcePath, metadata)) {
        _status = -1;
        return _status;
    }
    if (_options.target == AudioTarget::Mp3) {
        const auto audio = std::find_if(metadata.streams.begin(), metadata.streams.end(),
            [](const Stream& stream) { return stream.codec_type && *stream.codec_type == "audio"; });
        if (_options.mp3Quality < 0 || _options.mp3Quality > 9 ||
            _targetPath.extension() != ".mp3" || _targetTMPPath.extension() != ".mp3" ||
            !audio->codec_name || *audio->codec_name != "flac" ||
            !audio->channels || *audio->channels < 1 ||
            (*audio->channels > 2 && !_options.downmixToStereo)) {
            spdlog::error("MP3 requires FLAC input, VBR quality 0..9 and mono/stereo audio "
                "(use --downmix-to-stereo to explicitly allow multichannel input).");
            _status = -1;
            return _status;
        }
        std::error_code pathError;
        const auto canonicalSource = fs::weakly_canonical(_sourcePath, pathError);
        if (pathError) {
            spdlog::error("Cannot resolve source path: {}", pathError.message());
            _status = -1;
            return _status;
        }
        const auto canonicalTarget = fs::weakly_canonical(_targetPath, pathError);
        if (pathError) {
            spdlog::error("Cannot resolve target path: {}", pathError.message());
            _status = -1;
            return _status;
        }
        for (const auto& path : { _targetPath, _targetTMPPath, sidecarPath(_targetPath),
            sidecarPath(_targetTMPPath), temporaryInputPath(_targetTMPPath) }) {
            const auto canonical = fs::weakly_canonical(path, pathError);
            if (pathError) {
                spdlog::error("Cannot resolve MP3 destination '{}': {}",
                    CommonUtils::path_to_utf8(path), pathError.message());
                _status = -1;
                return _status;
            }
            const bool exists = fs::exists(path, pathError);
            if (pathError) {
                spdlog::error("Cannot inspect destination '{}': {}",
                    CommonUtils::path_to_utf8(path), pathError.message());
                _status = -1;
                return _status;
            }
            const bool sourceAlias = exists && fs::equivalent(path, _sourcePath, pathError);
            const bool temporary = path == _targetTMPPath || path == sidecarPath(_targetTMPPath) ||
                path == temporaryInputPath(_targetTMPPath);
            if (pathError || canonical == canonicalSource || sourceAlias ||
                (temporary && exists) || (path == _targetTMPPath && canonical == canonicalTarget)) {
                spdlog::error("Unsafe MP3 destination: {}", CommonUtils::path_to_utf8(path));
                _status = -1;
                return _status;
            }
        }
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
    bool preparedInput = false;
    if (_options.target == AudioTarget::Mp3) {
        const bool sidecarExists = fs::exists(sidecarPath(_targetPath), ec);
        if (ec || (!_overwriteTarget && sidecarExists)) {
            spdlog::error("Cannot use metadata destination '{}': {}",
                CommonUtils::path_to_utf8(sidecarPath(_targetPath)), ec ? ec.message() : "already exists");
            _status = -1;
            return _status;
        }
        if (!FlacMetadata::Save(_sourcePath, sidecarPath(_targetTMPPath),
            temporaryInputPath(_targetTMPPath), preparedInput)) {
            removeTemporary(sidecarPath(_targetTMPPath));
            removeTemporary(temporaryInputPath(_targetTMPPath));
            _status = -1;
            return _status;
        }
    }

    std::wstring executable{ L"ffmpeg" };
    const auto settings = AppSettingsJson::AppSetting();
    if (settings && !settings->FLACSettings.ffmpeg_exe_name.empty()) {
        executable = settings->FLACSettings.ffmpeg_exe_name;
    }

    spdlog::info("Processing: {}", CommonUtils::utf8string_to_string(_sourcePath.u8string()));
    const auto arguments = conversionArguments(_options, metadata);
    const int result = runFFmpeg(executable, preparedInput ? temporaryInputPath(_targetTMPPath) : _sourcePath,
        _targetTMPPath, _options.target == AudioTarget::Mp3 ? false : _overwriteTarget, arguments);
    if (preparedInput) {
        removeTemporary(temporaryInputPath(_targetTMPPath));
    }
    if (result != 0) {
        if (result != -1) {
            spdlog::error("FFmpeg failed with exit code {} for '{}'",
                result, CommonUtils::utf8string_to_string(_sourcePath.u8string()));
        }
        removeTemporary(_targetTMPPath);
        if (_options.target == AudioTarget::Mp3) {
            removeTemporary(sidecarPath(_targetTMPPath));
        }
        _status = -1;
        return _status;
    }
    if (_options.target == AudioTarget::Mp3) {
        const auto output = FFmpeg::GetFFprobeMetadataAPI(_targetTMPPath);
        const auto audio = std::find_if(output.streams.begin(), output.streams.end(),
            [](const Stream& stream) { return stream.codec_type && *stream.codec_type == "audio"; });
        const auto sourceAudio = std::find_if(metadata.streams.begin(), metadata.streams.end(),
            [](const Stream& stream) { return stream.codec_type && *stream.codec_type == "audio"; });
        const auto sourceRate = parseInteger(sourceAudio->sample_rate);
        const int expectedRate = *sourceRate == 44100 ? 44100 : 48000;
        const int expectedChannels = (std::min)(*sourceAudio->channels, 2);
        if (audio == output.streams.end() || !audio->codec_name || *audio->codec_name != "mp3" ||
            parseInteger(audio->sample_rate) != expectedRate || audio->channels != expectedChannels ||
            !output.format.duration || *output.format.duration <= 0 ||
            output.format.nb_chapters != metadata.format.nb_chapters ||
            (metadata.format.duration && std::abs(*metadata.format.duration - *output.format.duration) > 0.15) ||
            std::count_if(output.streams.begin(), output.streams.end(), [](const Stream& stream) {
                return stream.disposition && stream.disposition->attached_pic;
            }) != std::count(arguments.begin(), arguments.end(), "-map") - 1) {
            spdlog::error("MP3 output verification failed for '{}'.", CommonUtils::path_to_utf8(_targetTMPPath));
            removeTemporary(_targetTMPPath);
            removeTemporary(sidecarPath(_targetTMPPath));
            _status = -1;
            return _status;
        }
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


bool MediaConvertionTask::InstallOutputFile(const fs::path& temporary, const fs::path& target, bool overwrite)
{
    return installFile(temporary, target, overwrite);
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

    fs::path backup = sidecarPath(_targetTMPPath);
    backup += ".backup";
    bool backedUp = false;
    if (_options.target == AudioTarget::Mp3) {
        if (_overwriteTarget && fs::exists(sidecarPath(_targetPath), ec)) {
            backedUp = fs::copy_file(sidecarPath(_targetPath), backup, fs::copy_options::none, ec);
            if (!backedUp) {
                spdlog::error("Cannot back up existing metadata sidecar: {}", ec.message());
                _status = -1;
                return _status;
            }
        }
        if (ec || !installFile(sidecarPath(_targetTMPPath), sidecarPath(_targetPath), _overwriteTarget)) {
            if (ec) {
                spdlog::error("Cannot inspect existing metadata sidecar: {}", ec.message());
            }
            if (backedUp) {
                removeTemporary(backup);
            }
            _status = -1;
            return _status;
        }
    }
    if (!installFile(_targetTMPPath, _targetPath, _overwriteTarget)) {
        spdlog::error("Converted audio remains recoverable at '{}'.", CommonUtils::path_to_utf8(_targetTMPPath));
        if (_options.target == AudioTarget::Mp3) {
            if (backedUp) {
                if (!installFile(backup, sidecarPath(_targetPath), true)) {
                    spdlog::error("Original metadata remains recoverable at '{}'.",
                        CommonUtils::path_to_utf8(backup));
                }
            }
            else {
                removeTemporary(sidecarPath(_targetPath));
            }
        }
        _status = -1;
        return _status;
    }
    if (backedUp) {
        removeTemporary(backup);
    }
    if (_options.target == AudioTarget::Mp3) {
        spdlog::info("Installed MP3 and original-metadata sidecar: '{}', '{}'.",
            CommonUtils::path_to_utf8(_targetPath), CommonUtils::path_to_utf8(sidecarPath(_targetPath)));
    }

    _status = 0;
    return _status;
}
