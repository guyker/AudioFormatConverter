
#include "MediaConvertionTask.h"

#include <algorithm>
#include <string>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include "AppSettings.h"
#include "CommonUtils.h"
#include "ffmpeg.h"
#include "PlatformUtils.h"
#include "FlacMetadata.h"
#include "MediaTrack.h"

namespace fs = std::filesystem;

namespace
{
void logFFmpegOutput(std::string& pending, std::string_view chunk, const fs::path& source)
{
    pending.append(chunk);
    std::size_t newline = 0;
    while ((newline = pending.find('\n')) != std::string::npos) {
        auto line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            spdlog::warn("FFmpeg [{}]: {}", CommonUtils::path_to_utf8(source), line);
        }
    }
    constexpr std::size_t maxPendingBytes = 8192;
    while (pending.size() >= maxPendingBytes) {
        spdlog::warn("FFmpeg [{}]: {}", CommonUtils::path_to_utf8(source),
            pending.substr(0, maxPendingBytes));
        pending.erase(0, maxPendingBytes);
    }
}

void flushFFmpegOutput(std::string& pending, const fs::path& source)
{
    if (!pending.empty() && pending.back() == '\r') pending.pop_back();
    if (!pending.empty()) {
        spdlog::warn("FFmpeg [{}]: {}", CommonUtils::path_to_utf8(source), pending);
    }
}

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
    const bool isAac = audioStream->codec_name && *audioStream->codec_name == "aac";
    std::vector<std::string> issues;
    if (!sampleRate) {
        issues.emplace_back("sample rate could not be verified");
    }
    else if (*sampleRate < 44100) {
        issues.emplace_back("sample rate " + std::to_string(*sampleRate) + " Hz is below 44100 Hz");
    }
    if (!isAac) {
        const auto bitDepth = audioStream->bits_per_sample;
        if (!bitDepth) {
            issues.emplace_back("bit depth could not be verified");
        }
        else if (*bitDepth < 16) {
            issues.emplace_back("bit depth " + std::to_string(*bitDepth) + " is below 16-bit");
        }
    }

    if (issues.empty()) {
        if (isAac) {
            spdlog::warn("AAC source '{}' is lossy; conversion cannot restore quality lost during AAC encoding.",
                CommonUtils::path_to_utf8(source));
        }
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

fs::path sidecarPath(const fs::path& audio, MetadataSidecarFormat format)
{
    return FlacMetadata::SidecarPath(audio, format);
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
    for (const auto& stream : metadata.streams) {
        if (!stream.disposition || !stream.disposition->attached_pic) {
            continue;
        }
        const auto& codec = stream.codec_name;
        if (codec && (*codec == "mjpeg" || *codec == "png" || *codec == "gif" ||
            *codec == "tiff" || *codec == "bmp" || *codec == "webp" || *codec == "jpegxl")) {
            if (stream.index) {
                arguments.insert(arguments.end(), { "-map", "0:" + std::to_string(*stream.index) });
            }
            else {
                spdlog::warn("Picture stream has no index and cannot be mapped; original native FLAC pictures remain in the metadata sidecar.");
            }
        }
        else {
            spdlog::warn("Picture stream {} cannot be embedded; original native FLAC pictures remain in the metadata sidecar.",
                stream.index.value_or(-1));
        }
    }
    arguments.insert(arguments.end(), { "-map_metadata", "0", "-map_chapters", "0", "-c:v", "copy" });
    if (options.target == AudioTarget::Flac) {
        arguments.insert(arguments.end(), {
            "-af", "aresample=osf=s16:osr=44100:dither_method=triangular", "-c:a", "flac" });
        return arguments;
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
    arguments.insert(arguments.end(), { "-c:a", "libmp3lame", "-q:a", std::to_string(options.mp3Quality),
        "-id3v2_version", "4", "-write_id3v1", "0", "-write_xing", "1" });
    return arguments;
}

int runFFmpeg(const std::wstring& executable, const fs::path& source, const fs::path& destination,
    bool overwrite, const std::vector<std::string>& conversion, const fs::path& diagnosticSource)
{
#ifdef _WIN32
    std::vector<std::wstring> arguments{ executable, L"-nostdin", L"-xerror", L"-i",
        PlatformUtils::WindowsLongPath(source) };
    for (const auto& argument : conversion) {
        arguments.push_back(CommonUtils::utf8ToWstring(argument));
    }
    arguments.insert(arguments.end(), { overwrite ? L"-y" : L"-n", L"-v", L"warning",
        L"-nostats", PlatformUtils::WindowsLongPath(destination) });
    // The Windows CRT joins spawn arguments into a command line without quoting them.
    std::vector<std::wstring> quotedArguments;
    quotedArguments.reserve(arguments.size());
    for (const auto& argument : arguments) {
        quotedArguments.push_back(quoteWindowsArgument(argument));
    }
    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0) ||
        !SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        const auto error = GetLastError();
        if (readPipe) CloseHandle(readPipe);
        if (writePipe) CloseHandle(writePipe);
        spdlog::error("Cannot create FFmpeg diagnostic pipe: {}",
            std::system_category().message(static_cast<int>(error)));
        return -1;
    }

    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        spdlog::error("Cannot open null input for FFmpeg: {}",
            std::system_category().message(static_cast<int>(error)));
        return -1;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION process{};
    std::wstring commandLine;
    for (const auto& argument : quotedArguments) {
        if (!commandLine.empty()) commandLine.push_back(L' ');
        commandLine += argument;
    }
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');
    const BOOL created = CreateProcessW(nullptr, mutableCommandLine.data(),
        nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process);
    const auto createError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(writePipe);
    CloseHandle(nullInput);
    if (!created) {
        CloseHandle(readPipe);
        spdlog::error("Failed to launch FFmpeg: {}",
            std::system_category().message(static_cast<int>(createError)));
        return -1;
    }

    std::string pending;
    char buffer[4096];
    DWORD bytesRead = 0;
    bool captureFailed = false;
    DWORD captureError = ERROR_SUCCESS;
    for (;;) {
        if (!ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr)) {
            captureError = GetLastError();
            captureFailed = captureError != ERROR_BROKEN_PIPE;
            break;
        }
        if (bytesRead == 0) break;
        logFFmpegOutput(pending, std::string_view(buffer, bytesRead), diagnosticSource);
    }
    flushFFmpegOutput(pending, diagnosticSource);
    CloseHandle(readPipe);

    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    const DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    DWORD exitCode = 0;
    const bool gotExitCode = GetExitCodeProcess(process.hProcess, &exitCode) != 0;
    const DWORD exitError = gotExitCode ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (waitResult != WAIT_OBJECT_0) {
        spdlog::error("Failed while waiting for FFmpeg: {}",
            std::system_category().message(static_cast<int>(waitError)));
        return -1;
    }
    if (!gotExitCode) {
        spdlog::error("Cannot read FFmpeg exit code: {}",
            std::system_category().message(static_cast<int>(exitError)));
        return -1;
    }
    if (captureFailed) {
        spdlog::error("Failed to capture FFmpeg diagnostics: {}",
            std::system_category().message(static_cast<int>(captureError)));
        return -1;
    }
    return static_cast<int>(exitCode);
#else
    const std::string executableUtf8 = PlatformUtils::wstringToUtf8_ver2(executable);
    std::vector<std::string> arguments{ executableUtf8, "-nostdin", "-xerror", "-i", source.string() };
    arguments.insert(arguments.end(), conversion.begin(), conversion.end());
    arguments.insert(arguments.end(), {
        overwrite ? "-y" : "-n", "-v", "warning", "-nostats", destination.string() });
    std::vector<char*> pointers;
    pointers.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        pointers.push_back(const_cast<char*>(argument.c_str()));
    }
    pointers.push_back(nullptr);

    int diagnosticPipe[2]{};
    if (pipe(diagnosticPipe) != 0) {
        spdlog::error("Cannot create FFmpeg diagnostic pipe: {}",
            std::system_category().message(errno));
        return -1;
    }
    const pid_t child = fork();
    if (child == -1) {
        close(diagnosticPipe[0]);
        close(diagnosticPipe[1]);
        spdlog::error("Failed to launch FFmpeg: {}", std::system_category().message(errno));
        return -1;
    }
    if (child == 0) {
        close(diagnosticPipe[0]);
        if (dup2(diagnosticPipe[1], STDERR_FILENO) == -1 ||
            dup2(diagnosticPipe[1], STDOUT_FILENO) == -1) {
            _exit(126);
        }
        close(diagnosticPipe[1]);
        execvp(executableUtf8.c_str(), pointers.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    close(diagnosticPipe[1]);
    std::string pending;
    char buffer[4096];
    ssize_t bytesRead = 0;
    bool captureFailed = false;
    int captureError = 0;
    for (;;) {
        bytesRead = read(diagnosticPipe[0], buffer, sizeof(buffer));
        if (bytesRead == -1 && errno == EINTR) continue;
        if (bytesRead == -1) {
            captureFailed = true;
            captureError = errno;
            break;
        }
        if (bytesRead == 0) break;
        logFFmpegOutput(pending, std::string_view(buffer, static_cast<std::size_t>(bytesRead)), diagnosticSource);
    }
    flushFFmpegOutput(pending, diagnosticSource);
    close(diagnosticPipe[0]);

    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            spdlog::error("Failed while waiting for FFmpeg: {}", std::system_category().message(errno));
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        if (captureFailed) {
            spdlog::error("Failed to capture FFmpeg diagnostics: {}",
                std::system_category().message(captureError));
            return -1;
        }
        return WEXITSTATUS(status);
    }
    if (captureFailed) {
        spdlog::error("Failed to capture FFmpeg diagnostics: {}",
            std::system_category().message(captureError));
        return -1;
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
    const bool parseOriginalMetadata = FlacMetadata::AppliesTo(_sourcePath, _options);
    _preserveOriginalMetadata = parseOriginalMetadata &&
        _options.sidecarFormat != MetadataSidecarFormat::None;
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
    }
    {
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
        std::vector<fs::path> destinations{ _targetPath, _targetTMPPath };
        if (_preserveOriginalMetadata) {
            destinations.insert(destinations.end(), { sidecarPath(_targetPath, _options.sidecarFormat),
                sidecarPath(_targetTMPPath, _options.sidecarFormat) });
        }
        if (parseOriginalMetadata) {
            destinations.push_back(temporaryInputPath(_targetTMPPath));
        }
        for (const auto& path : destinations) {
            const auto canonical = fs::weakly_canonical(path, pathError);
            if (pathError) {
                spdlog::error("Cannot resolve conversion destination '{}': {}",
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
            const bool temporary = path == _targetTMPPath || path == temporaryInputPath(_targetTMPPath) ||
                (_preserveOriginalMetadata && path == sidecarPath(_targetTMPPath, _options.sidecarFormat));
            if (pathError || canonical == canonicalSource || sourceAlias ||
                (temporary && exists) || (path == _targetTMPPath && canonical == canonicalTarget)) {
                spdlog::error("Unsafe conversion destination: {}", CommonUtils::path_to_utf8(path));
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
    if (parseOriginalMetadata) {
        _failureType = "PRESERVATION";
        const auto sidecar = sidecarPath(_targetPath, _options.sidecarFormat);
        const auto temporarySidecar = sidecarPath(_targetTMPPath, _options.sidecarFormat);
        const bool sidecarExists = _preserveOriginalMetadata && fs::exists(sidecar, ec);
        if (ec || (!_overwriteTarget && sidecarExists)) {
            spdlog::error("Cannot use metadata destination '{}': {}",
                CommonUtils::path_to_utf8(sidecar), ec ? ec.message() : "already exists");
            _status = -1;
            return _status;
        }
        const auto readable = _options.sidecarFormat == MetadataSidecarFormat::Json
            ? MediaTrack::toJsonString(metadata) : std::string{};
        // With sidecars disabled, Save only prepares a decoder input without appended tags.
        if (!FlacMetadata::Save(_sourcePath, temporarySidecar, _options.sidecarFormat, readable,
            temporaryInputPath(_targetTMPPath), preparedInput) ||
            (_preserveOriginalMetadata &&
                !FlacMetadata::Verify(_sourcePath, temporarySidecar, _options.sidecarFormat))) {
            if (_preserveOriginalMetadata) {
                removeTemporary(temporarySidecar);
            }
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

    spdlog::debug("Processing: {}", CommonUtils::path_to_utf8(_sourcePath));
    _failureType = "AUDIO";
    const auto arguments = conversionArguments(_options, metadata);
    const int result = runFFmpeg(executable, preparedInput ? temporaryInputPath(_targetTMPPath) : _sourcePath,
        _targetTMPPath, false, arguments, _sourcePath);
    if (preparedInput) {
        removeTemporary(temporaryInputPath(_targetTMPPath));
    }
    if (result != 0) {
        if (result != -1) {
            spdlog::error("FFmpeg failed with exit code {} for '{}'",
                result, CommonUtils::utf8string_to_string(_sourcePath.u8string()));
        }
        removeTemporary(_targetTMPPath);
        if (_preserveOriginalMetadata) {
            removeTemporary(sidecarPath(_targetTMPPath, _options.sidecarFormat));
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
            (metadata.format.duration && std::abs(*metadata.format.duration - *output.format.duration) > 0.15)) {
            spdlog::error("MP3 output verification failed for '{}'.", CommonUtils::path_to_utf8(_targetTMPPath));
            removeTemporary(_targetTMPPath);
            if (_preserveOriginalMetadata) {
                removeTemporary(sidecarPath(_targetTMPPath, _options.sidecarFormat));
            }
            _status = -1;
            return _status;
        }
    }
    else {
        const auto output = FFmpeg::GetFFprobeMetadataAPI(_targetTMPPath);
        const auto audio = std::find_if(output.streams.begin(), output.streams.end(),
            [](const Stream& stream) { return stream.codec_type && *stream.codec_type == "audio"; });
        const auto sourceAudio = std::find_if(metadata.streams.begin(), metadata.streams.end(),
            [](const Stream& stream) { return stream.codec_type && *stream.codec_type == "audio"; });
        if (audio == output.streams.end() || !audio->codec_name || *audio->codec_name != "flac" ||
            parseInteger(audio->sample_rate) != 44100 || audio->bits_per_sample != 16 ||
            audio->channels != sourceAudio->channels || !output.format.duration || *output.format.duration <= 0 ||
            (metadata.format.duration && std::abs(*metadata.format.duration - *output.format.duration) > 0.15)) {
            spdlog::error("FLAC output verification failed for '{}'.", CommonUtils::path_to_utf8(_targetTMPPath));
            removeTemporary(_targetTMPPath);
            if (_preserveOriginalMetadata) {
                removeTemporary(sidecarPath(_targetTMPPath, _options.sidecarFormat));
            }
            _status = -1;
            return _status;
        }
    }

    _failureType = "PRESERVATION";
    const bool verifiedArchive = _preserveOriginalMetadata &&
        FlacMetadata::Verify(_sourcePath, sidecarPath(_targetTMPPath, _options.sidecarFormat), _options.sidecarFormat);
    if (_preserveOriginalMetadata && !verifiedArchive) {
        removeTemporary(_targetTMPPath);
        removeTemporary(sidecarPath(_targetTMPPath, _options.sidecarFormat));
        _status = -1;
        return _status;
    }
    _audit = PreservationAudit::Compare(_sourcePath, _targetTMPPath, verifiedArchive);
    if (!_audit.ok) {
        removeTemporary(_targetTMPPath);
        if (_preserveOriginalMetadata) removeTemporary(sidecarPath(_targetTMPPath, _options.sidecarFormat));
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
    _failureType = "IO";
    if (_status == -1) {
        return _status;
    }

    std::error_code ec;
    if (!fs::is_regular_file(_targetTMPPath, ec) || ec) {
        spdlog::error("Converted temporary output is missing: {}", CommonUtils::utf8string_to_string(_targetTMPPath.u8string()));
        _status = -1;
        return _status;
    }

    fs::path backup = sidecarPath(_targetTMPPath, _options.sidecarFormat);
    backup += ".backup";
    bool backedUp = false;
    if (_preserveOriginalMetadata) {
        if (_overwriteTarget && fs::exists(sidecarPath(_targetPath, _options.sidecarFormat), ec)) {
            backedUp = fs::copy_file(sidecarPath(_targetPath, _options.sidecarFormat), backup, fs::copy_options::none, ec);
            if (!backedUp) {
                spdlog::error("Cannot back up existing metadata sidecar: {}", ec.message());
                _status = -1;
                return _status;
            }
        }
        if (ec || !installFile(sidecarPath(_targetTMPPath, _options.sidecarFormat), sidecarPath(_targetPath, _options.sidecarFormat), _overwriteTarget)) {
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
        if (_preserveOriginalMetadata) {
            if (backedUp) {
                if (!installFile(backup, sidecarPath(_targetPath, _options.sidecarFormat), true)) {
                    spdlog::error("Original metadata remains recoverable at '{}'.",
                        CommonUtils::path_to_utf8(backup));
                }
            }
            else {
                removeTemporary(sidecarPath(_targetPath, _options.sidecarFormat));
            }
        }
        _status = -1;
        return _status;
    }
    if (backedUp) {
        removeTemporary(backup);
    }
    if (_preserveOriginalMetadata) {
        spdlog::debug("Installed audio and original-metadata sidecar: '{}', '{}'.",
            CommonUtils::path_to_utf8(_targetPath), CommonUtils::path_to_utf8(sidecarPath(_targetPath, _options.sidecarFormat)));
    }

    _status = 0;
    return _status;
}
