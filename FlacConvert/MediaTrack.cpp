#include "MediaTrack.h"



#include <iostream>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <array>
#include <cstdlib>
#include <cwchar>

#ifdef _WIN32
#include <windows.h>
// On Windows, use _wpopen/_pclose which accept wide strings.
#define popen _wpopen
#define pclose _pclose




std::wstring getAudioMetadataJSON(const std::wstring& filePath) {
    std::wstring command = L"ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters \"" + filePath + L"\"";

#ifdef _WIN32
    command += L" 2>&1"; // Redirect stderr to stdout (Windows)
    FILE* pipe = _wpopen(command.c_str(), L"r");
#else
    command += L" 2>&1"; // Redirect stderr to stdout (Linux/macOS)
    FILE* pipe = popen(std::string(command.begin(), command.end()).c_str(), "r");
#endif

    if (!pipe) {
        std::wcerr << L"Failed to run ffprobe!" << std::endl;
        return L"";
    }

    std::ostringstream result;
    char buffer[2024];

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Convert UTF-8 JSON output to wstring
    //return CommonUtils::utf8ToWstring(result.str());
    return CommonUtils::utf8ToWstring(result.str());
}


// Run ffprobe using a wide-string command and capture its output as a wide string.
std::wstring runFFprobe(const std::wstring& filename) {

    auto ret = getAudioMetadataJSON(filename);

    return ret;

    //// Build the command (including quoting the filename)
    //std::wstring command = L"ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters \"";
    //command += filename;
    //command += L"\"";

    //std::array<wchar_t, 2024> buffer;
    //std::wstring outputWide;
    //FILE* pipe = popen(command.c_str(), L"r");
    //if (!pipe)
    //    throw std::runtime_error("Failed to open pipe");
    //while (fgetws(buffer.data(), static_cast<int>(buffer.size()), pipe))
    //    outputWide += buffer.data();
    //pclose(pipe);
    //// Convert the wide-character output (assumed to be UTF‑16) to UTF‑8.
    //return WideToUTF8_2(outputWide);
}

#else  // Linux/Unix

#include <clocale>
#include <cwchar>

// On Linux, set the locale to a UTF‑8–compatible one and convert the wide filename using wcstombs.
std::string runFFprobe(const std::wstring& filename) {
    // Ensure that the locale is set to UTF‑8.
    std::setlocale(LC_CTYPE, "en_US.UTF-8");

    // Convert the wide filename to a UTF‑8 encoded narrow string.
    size_t len = std::wcstombs(nullptr, filename.c_str(), 0);
    if (len == static_cast<size_t>(-1))
        throw std::runtime_error("wcstombs conversion error");
    std::string narrowFilename(len, '\0');
    std::wcstombs(&narrowFilename[0], filename.c_str(), len);

    // Build the command.
    std::string command = "ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters \"";
    command += narrowFilename;
    command += "\"";

    std::array<char, 1024> buffer;
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        throw std::runtime_error("Failed to open pipe");
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        output += buffer.data();
    pclose(pipe);
    return output;
}

#endif




//returns media information (json string and media objec) from a media file (on file system)
std::tuple<MediaInformation, std::wstring> MediaTrack::ReadMediaInfoFromFile(std::filesystem::path mediaFilePath)
{
    std::size_t hashNumber = std::hash<std::wstring>{}(mediaFilePath);
    auto tmpFile = std::format("tmp_media_{}.json", hashNumber);

    try
    {
        auto jsonString = MediaTrack::ExtractMetadataFromMediaTrack(mediaFilePath, tmpFile);
        auto mi = MediaTrack::ParseMediaTrack(jsonString);

        return std::make_tuple(mi, jsonString);
    }
    catch (const std::exception& ex) {
        std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << ex.what() << std::endl;

    }

    return std::make_tuple(MediaInformation{}, L"{}");
}

//create a media file (on filesystem) from a media track
std::wstring MediaTrack::ExtractMetadataFromMediaTrack(std::filesystem::path mediaFilePath, std::filesystem::path outFile)
{
    using namespace std::string_literals;


    int status = 0;

    auto tmpPath = fs::temp_directory_path();
    //fs::path tmpFilePath{ tmpPath.generic_wstring() + L"\\media_info.json"s };
    fs::path tmpFilePath{ tmpPath / outFile };


    std::wstring cmdExecNameW{ L"ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters "s };
    std::wstring commandW{ cmdExecNameW + L"\""s + mediaFilePath.generic_wstring() + L"\""s + L" > \""s + tmpFilePath.generic_wstring() + L"\""s };

    //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + L"'" + _targetTMPPath.generic_wstring() + L"'" };


    try
    {
        // Specify your file name as a wide string.
   //     std::wstring filename = L"input.mp3";
#ifdef _WIN32
        std::wstring f = mediaFilePath.generic_wstring();

        std::wstring wide_output = runFFprobe(f);
        // Convert wide output to UTF-8 narrow string for printing.
     //   std::cout << wide_output << std::endl;
#else
        std::string output = runFFprobe(filename);
        std::cout << output << std::endl;
#endif

        if (status == 0)
        {

            return wide_output;
        }

    }
    catch (const std::exception& e) {
        std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << e.what() << std::endl;
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return L"**ERROR***";
}


MediaInformation MediaTrack::ParseMediaTrack(std::wstring jsonString)
{

    MediaInformation mediaInfo;

    rapidjson::Document doc;
    std::string utf8Json = CommonUtils::wstringToUtf8(jsonString);
    doc.Parse(utf8Json.c_str());

    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: " << doc.GetParseError() << std::endl;

        return mediaInfo;
    }


    if (doc.IsObject() && doc.HasMember("format"))
    {
        return MediaInformation{ MediaTrack::ParseMediaInformation(doc["format"]) };
    }

    return mediaInfo;
}
