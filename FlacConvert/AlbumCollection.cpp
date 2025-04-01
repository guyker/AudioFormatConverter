
#include <ranges>
#include <algorithm> 

#include "AlbumCollection.h"
#include "FolderConvert.h"
#include "JsonUtils.h"
#include "MediaTrack.h"

namespace fs = std::filesystem;
using namespace rapidjson;


const std::vector<char> ProgressCircleChars { '|', '/', '-', '\\' };

//constexpr auto CLEAR_LINE{ L"\x1b[H\x1b[J" };


//
//AlbumCollection::AlbumCollection(DirectoryContentEntryList const& albumList) : _AlbumList{ albumList }
//{
//}
//
//AlbumCollection::AlbumCollection(DirectoryContentEntryList && albumList) : _AlbumList{ albumList }
//{
//}


void AlbumCollection::Clear()
{
    _AlbumList.clear();
}

bool AlbumCollection::LoadAlbumCollection(DirectoryContentEntryList& albumList)
{
	_AlbumList = albumList;
    return true;

}

bool AlbumCollection::LoadAlbumCollection(std::filesystem::path albumCollectionDirPath)
{
    auto startTime = std::chrono::steady_clock::now();
    std::cout << "Scanning collection... ";

    //Scan directory and load all tracks location
    LoadAlbumFromCurrentFolder(albumCollectionDirPath, 9);

    auto endTime = std::chrono::steady_clock::now();
    std::cout << std::format("Completed, Albums: {} [{}ms]", _AlbumList.size(), std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()) << std::endl;

    return true;
}

//Load all all albumes and tracks into _fileList
bool AlbumCollection::LoadAlbumCollectionWithMetadata(std::filesystem::path albumCollectionDirPath, std::filesystem::path& outDirPath)
{

    //Scan directory and load all tracks location
    LoadAlbumCollection(albumCollectionDirPath);

    //For each loaded Albunm/Track, load/reload all media information 
    ExportMediaInformationToDB();

    //Save Media Information ingo a JSON file
    SaveAlbumCollectionToJSONFile(outDirPath);

    return _AlbumList.size() > 0;
}



TrackInfoList AlbumCollection::LoadAlbumFromCurrentFolder(std::filesystem::path path, int depth)
{
    //Empty list to store all potential tracks under the current directory (path)
    TrackInfoList currentDirTrackList;

    if (depth == 0) // reached max recursive depth
    {
        return currentDirTrackList;
    }

    if (fs::exists(path)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                //Scan directory and return the list of files under the directory entry (one level).

                auto trackList = LoadAlbumFromCurrentFolder(entry.path(), depth - 1);

                //check if the current folder has at least one track and add it to a new Album
                if (trackList.size() > 0)
                {
                    _AlbumList.push_back({ entry, trackList });
                }
            }
            else {
                if (MediaTrack::IsFileAcceptedAudioFile(entry))
                {
                    auto path2Fixed = entry.path().lexically_normal().native();
                    long long fileSize = fs::file_size(path2Fixed);

                    auto fileName = entry.path().filename();
                    currentDirTrackList.push_back({ fileName, fileSize, MediaInformation{}, std::wstring{L"{}"}});
                }
            }
        }
    }
    else
    {
     //   std::cout << std::format("***Error: Media library not found: : {}", path) << std::endl;
    }

    return currentDirTrackList;
}





//std::mutex mtx;


//Load all media media information from the preloaded album list (_AlbumList)
size_t AlbumCollection::ExportMediaInformationToDB(bool bAsync)
{
    int albumCount = 0;
    int progressIndex = 0;

    for (auto& [albumPath, trackList] : _AlbumList)
    {
        std::cout << "\r\033[K";
        //std::wcout << std::format(L"{} Processing [{}/{}]: {}", ProgressCircleChars[progressIndex], ++albumCount, _AlbumList.size(), albumPath.path().generic_wstring());
        std::string str{ "???" };
        try
        {
            //str = RemoveSpecialCharacter(albumPath.path().generic_string());
            str = albumPath.path().generic_string();
        }
        catch (...) {}
        std::cout << std::format("{} Processing [{}/{}]: {}", ProgressCircleChars[progressIndex], ++albumCount, _AlbumList.size(), str);
        progressIndex = (progressIndex + 1) % ProgressCircleChars.size();

        //Album tracks list holder 
        std::vector<std::tuple<MediaLoadingFuture, MediaInformation&, std::wstring&>> asyncFutureList;

        for (auto& [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            if (MediaTrack::IsValidMedia(trackPath)) {
                auto path2Fixed = trackPath.lexically_normal().native();
              //  long long fileSize = fs::file_size(path2Fixed);

                if (bAsync)
                {
                  //  fs::path outfilePath{ trackName / fs::path("_" + TMP_MEDIA_JSON_FILE_NAME) };
                    //std::lock_guard<std::mutex> lock(mtx);
                    auto miFuture = std::async(std::launch::async, AlbumCollection::GetMediaInfoFromMediaFile, path2Fixed);

                  //  miFuture.get();

                    asyncFutureList.push_back({ std::move(miFuture), mediaInfo, mediaInfoString });

                    //if (asyncFutureList.size() > 4)
                    //{
                    //    for (auto& [furure_ret, mediaInfo, mediaInfoString] : asyncFutureList)
                    //    {
                    //        auto [mediaInfo_ret, mediaInfoString_ret] = furure_ret.get();
                    //        mediaInfo = mediaInfo_ret;
                    //        mediaInfoString = mediaInfoString_ret;
                    //    }
                    //    asyncFutureList.clear();
                    //}
                }
                else
                {
                    //fs::path outfilePath{ trackName / fs::path("_" + TMP_MEDIA_JSON_FILE_NAME) };
                    auto [mi_ret, jsonString_ret] = AlbumCollection::GetMediaInfoFromMediaFile(path2Fixed);
                    mediaInfoString = jsonString_ret;
                    mediaInfo = mi_ret;
                }
            }
        }

        for (auto& [furure_ret, mediaInfo, mediaInfoString] : asyncFutureList)
        {
            auto [mediaInfo_ret, mediaInfoString_ret] = furure_ret.get();
            mediaInfo = mediaInfo_ret;
            mediaInfoString = mediaInfoString_ret;
        }

      //  std::wcout << "\r\033[K";
    }

    return _AlbumList.size();
}


bool AlbumCollection::SaveAlbumCollectionToJSONFile(std::filesystem::path path)
{
    rapidjson::Document mediaDoc;
    mediaDoc.SetObject();

    for (auto [albumPath, trackList] : _AlbumList)
    {
        //Album tracks list holder 
        rapidjson::Value trackMediaArray(rapidjson::kArrayType);

        for (auto [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            auto hasExtension = trackPath.has_extension();
            auto fileEextension = trackPath.extension();
            // std::wstring entryPath{ trackPath.wstring() };
            if (trackPath.has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {

                rapidjson::Document trackDoc;
                std::string utf8Json = CommonUtils::wstringToUtf8(mediaInfoString);
                trackDoc.Parse(utf8Json.c_str());
                if (trackDoc.HasParseError()) {
                    std::cerr << "Error parsing JSON: " << trackDoc.GetParseError() << std::endl;                    
                }
                else
                {
					//Add media information to the track list
                    Value childValue;
                    childValue.CopyFrom(trackDoc, mediaDoc.GetAllocator()); // Copy childDoc into childValue using the parent allocator
                    trackMediaArray.PushBack(childValue, mediaDoc.GetAllocator());
                }
            }
        }

        if (trackMediaArray.Size() > 0)
        {
            try
            {
                ////track list exists add album
                //std::string name = albumPath.path().generic_string();
                //Value key(name.c_str(), mediaDoc.GetAllocator());
                //mediaDoc.AddMember(key, trackMediaArray, mediaDoc.GetAllocator());



                try
                {
                    //track list exists add album
                    std::wstring name = albumPath.path().wstring();
                    std::string utf8Key = CommonUtils::wstringToUtf8(name);
                    

                    Value key(utf8Key.c_str(), mediaDoc.GetAllocator());
                    mediaDoc.AddMember(key, trackMediaArray, mediaDoc.GetAllocator());
                }
                catch (const std::exception& ex) {
                    std::wcout << " ### EXCEOTION parsing json from ffmpeg: " << albumPath.path() << std::endl << ex.what() << std::endl;
                }

            }
            catch (...)
            {
                int i = 0;
            }
        }
    }


    if (fs::exists(path)) {
        std::error_code ec;
        if (fs::remove(path, ec)) {
        }
    }

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    mediaDoc.Accept(writer);
    const char* json = buffer.GetString();

    // Save the JSON string to a file
    std::ofstream file(path);
    if (file.is_open()) {
        file << json;
        file.close();

        //std::wcout << std::endl << std::format(L"====> Document saved to: {}", path.generic_wstring()) << std::endl;
    }
    else {
        std::cerr << "Unable to open file for writing" << std::endl;
        return false;
    }

    return true;
}






//ststic function that loads album list from a Json file and returns a DirectoryContentEntryList object
DirectoryContentEntryList AlbumCollection::LoadAlbumCollectionFromJSON(std::filesystem::path path, bool bBasicDataOnly)
{
    DirectoryContentEntryList albumList;

    if (!fs::exists(path)) {
        std::cout << "**** no file - json file not found Error parsing JSON: " << std::endl;
        return albumList;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cout << "****Error file=null - parsing JSON: " << std::endl;
        return albumList;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string utf8_data = buffer.str();
	std::wstring json = CommonUtils::utf8ToWstring(utf8_data);
    // Read the entire file into a string 
    //std::wstring json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    rapidjson::Document doc;

    // Parse the JSON data 
    std::string utf8Json = CommonUtils::wstringToUtf8(json);
    doc.Parse(utf8Json.c_str());

    // Check for parse errors 
    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: "
            << doc.GetParseError() << std::endl;

        return albumList;
    }

    bool isObject = doc.IsObject();
    auto jsonObject = doc.GetObj();


    //Albums
    int iAlbumCount = 0;
    for (auto itr = jsonObject.begin(); itr != jsonObject.end(); itr++)
    {
        TrackInfoList trackList;
        std::wstring albumName = CommonUtils::utf8ToWstring(itr->name.GetString());
        auto mediaTrackList = itr->value.GetArray();
        

        auto albumLogStr = std::format(L"Album [{}]: {}", ++iAlbumCount, albumName);
        //std::cout << albumLogStr << std::endl;
        std::wcout << albumLogStr << '\r';


        for (SizeType i = 0; i < mediaTrackList.Size(); i++)
        {
			auto& mediaTags = mediaTrackList[i];
            if (mediaTags.IsObject() && mediaTags.HasMember("format"))
            {
                MediaInformation mi{ MediaTrack::ParseMediaInformation(mediaTags["format"]) };
                if (bBasicDataOnly)
                {
                    trackList.push_back({ mi.filename, std::stol(mi.size), mi, L"{}" });
                }
                else
                {
                    trackList.push_back({ mi.filename, std::stol(mi.size), mi, json });
                }
            }
        }

        if (trackList.size() > 0)
        {
            //_AlbumList.push_back({ entry, trackList });
            std::filesystem::directory_entry entry{ albumName };
            albumList.push_back({ entry, trackList });
        }

    }

    return albumList;
}


//returns a json document from a json file (on file system) - from path
rapidjson::Document AlbumCollection::GetJSONDoc(std::filesystem::path mediaFilePath)
{
    rapidjson::Document doc;
    std::ifstream file(mediaFilePath);
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    doc.Parse(json.c_str());
    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: "
            << doc.GetParseError() << std::endl;

        return nullptr;
    }

    return doc;
}

 
 
//returns media information (json string and media objec) from a media file (on file system)
std::tuple<MediaInformation, std::wstring> AlbumCollection::GetMediaInfoFromMediaFile(std::filesystem::path mediaFilePath)
{
    std::size_t hashNumber = std::hash<std::wstring>{}(mediaFilePath);
    auto tmpFile = std::format("tmp_media_{}.json", hashNumber);

    try
    {
        auto jsonString = AlbumCollection::CreateMediaInfoFile(mediaFilePath, tmpFile);
       
        auto mi = MediaTrack::ParseMediaTrack(jsonString);

        //auto mi = AlbumCollection::ParseMediaInfoFromJsonFile(outPath);

        //std::string jsonString((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        return std::make_tuple(mi, jsonString);
    }    
    catch (const std::exception& ex) {
        std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << ex.what() << std::endl;

    }

    return std::make_tuple(MediaInformation{}, L"{}");
}





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

// Convert a wide string (UTF‑16) to a UTF‑8 std::string using Windows API.
std::string WideToUTF8_2(const std::wstring& wideStr) {
    if (wideStr.empty())
        return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0)
        throw std::runtime_error("Error converting wide string to UTF-8");
    std::string utf8Str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &utf8Str[0], sizeNeeded, nullptr, nullptr);
    // Remove the trailing null character if present.
    if (!utf8Str.empty() && utf8Str.back() == '\0')
        utf8Str.pop_back();
    return utf8Str;
}

//    // Function to convert UTF-8 string to wstring (cross-platform)
//    std::wstring utf8ToWstring(const std::string& str) {
//#ifdef _WIN32
//        if (str.empty()) return {};
//
//        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
//        std::wstring wstr(size_needed - 1, 0);
//        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
//
//        return wstr;
//#else
//        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
//        return converter.from_bytes(str);
//#endif
//    }

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
    char buffer[1024];

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





//create a media file (on filesystem) from a media track
std::wstring AlbumCollection::CreateMediaInfoFile(std::filesystem::path mediaFilePath, std::filesystem::path outFile)
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
        //    jsonDoc = AlbumCollection::GetJSONDoc(tmpFilePath);

            //if (fs::exists(tmpFilePath)) {
            //    std::error_code ec;
            //    if (fs::remove(tmpFilePath, ec)) {
            //    }
            //}

            return wide_output;
        }

    }
    catch (const std::exception& e) {
        std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << e.what() << std::endl;
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return L"**ERROR***";
}

#if 0
//create a media file (on filesystem) from a media track
std::filesystem::path AlbumCollection::CreateMediaInfoFile(std::filesystem::path mediaFilePath, std::filesystem::path outFile)
{
    using namespace std::string_literals;


    int status = 0;

    auto tmpPath = fs::temp_directory_path();
    //fs::path tmpFilePath{ tmpPath.generic_wstring() + L"\\media_info.json"s };
    fs::path tmpFilePath{ tmpPath / outFile };


    std::wstring cmdExecNameW{ L"ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters "s };
    std::wstring commandW{ cmdExecNameW + L"\""s + mediaFilePath.generic_wstring() + L"\""s + L" > \""s + tmpFilePath.generic_wstring() + L"\""s };

    //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + L"'" + _targetTMPPath.generic_wstring() + L"'" };

    rapidjson::Document jsonDoc = nullptr;

    try {
        //std::wcout << L"Getting media info:: " << mediaFilePath.generic_wstring() << std::endl;

        if (fs::exists(tmpFilePath)) {
            std::error_code ec;
            if (fs::remove(tmpFilePath, ec)) {
            }
        }

        status = _wsystem(commandW.c_str());
        //  status = std::system(commandW.c_str());

          ////std::string narrowCommand(commandW.begin(), commandW.end());
          //status = std::system(narrowCommand.c_str());




        if (status == 0)
        {
            //    jsonDoc = AlbumCollection::GetJSONDoc(tmpFilePath);

                //if (fs::exists(tmpFilePath)) {
                //    std::error_code ec;
                //    if (fs::remove(tmpFilePath, ec)) {
                //    }
                //}

            return tmpFilePath;
        }
        else
        {
            std::size_t hashNumber = std::hash<std::wstring>{}(mediaFilePath);
            auto tmpName = "tmp_media_" + std::to_string(hashNumber) + ".data";

            auto extension = mediaFilePath.extension();

            fs::path tmpFixFilePath{ tmpPath / tmpName };
            tmpFixFilePath.replace_extension(extension);

            try
            {
                fs::copy(mediaFilePath, tmpFixFilePath);
            }
            catch (const std::exception& ex) {
                std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << ex.what() << std::endl;

            }
            std::wstring commandWAlt{ cmdExecNameW + L"\""s + tmpFixFilePath.generic_wstring() + L"\""s + L" > \""s + tmpFilePath.generic_wstring() + L"\""s };

            status = _wsystem(commandWAlt.c_str());

            std::error_code ec;
            fs::remove(tmpFixFilePath, ec);

            if (status == 0)
            {
                return tmpFilePath;
            }
            else
            {
                int i = 0;
            }
            //  return tmpFilePath;
        }
    }
    catch (const std::exception& ex) {
        std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << ex.what() << std::endl;

    }

    return std::filesystem::path{};;
}
#endif



//-------------COMPARE


void AlbumCollection::SortByNumberOfTracks()
{
    std::ranges::stable_sort(_AlbumList, [](const auto& album1, const auto& album2) {
        const auto& [albumName1, trackList1] = album1;
        const auto& [albumName2, trackList2] = album2;

        return trackList2.size() > trackList1.size();
    });
}



SimilarDirectoryEntryList AlbumCollection::FindDuplicatedAlbums()
{
    SimilarDirectoryEntryList duplicatedAlbumList;

    if (_AlbumList.size() < 2)
    {
        return duplicatedAlbumList;
    }

    auto firstIt = _AlbumList.begin();
    auto secondIt = firstIt;
    secondIt++;

    auto appSettingPtr = AppSettingsJson::AppSetting();
	auto minMatchingTracksForDuplicate = appSettingPtr->MinMatchingTracksForDuplicate;
    while (firstIt != _AlbumList.end() && secondIt != _AlbumList.end())
    {
        bool bFound = false;
        auto& [dirEntry1, fileList1] = *firstIt;
        auto& [dirEntry2, fileList2] = *secondIt;

        auto pushedEndGroupIt = secondIt;
        int itemsInGroup{ 0 };
        auto fileList1Seize{ fileList1.size() };
        while (secondIt != _AlbumList.end() && fileList1.size() == fileList2.size() && fileList1.size() >= minMatchingTracksForDuplicate)
        {
            pushedEndGroupIt = secondIt;
            auto& [dirEntry2, fileList2] = *secondIt;

            secondIt++;
            bFound = true;
            itemsInGroup++;
        }

        secondIt = pushedEndGroupIt;

        auto firstIndex = std::ranges::distance(_AlbumList.cbegin(), firstIt);
        auto lastIndex = std::ranges::distance(_AlbumList.cbegin(), secondIt);

        if (bFound)
        {
            auto dupAlbums = FindDuplicationInGroup(_AlbumList, firstIt, secondIt);
            duplicatedAlbumList.insert(duplicatedAlbumList.end(), dupAlbums.begin(), dupAlbums.end());
            firstIt = secondIt;;
            secondIt++;
        }
        else
        {
            firstIt++;
            secondIt++;
        }
    }

    return duplicatedAlbumList;
}

SimilarDirectoryEntryList AlbumCollection::FindDuplicationInGroup(DirectoryContentEntryList& albumList, DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt)
{
    auto appSettingPtr = AppSettingsJson::AppSetting();
    auto sizeMatchPercentageThreshold = appSettingPtr->SizeMatchPercentageThreshold;

    SimilarDirectoryEntryList duplicatedAlbumList;

    if (firstIt != lastIt && firstIt != albumList.end() && lastIt != albumList.end())
    {
        auto currentIt = firstIt;
        while (currentIt != lastIt)
        {
            auto currentIt2 = currentIt;
            while (currentIt2 != lastIt)
            {
                currentIt2++;

                auto& [albumName1, trackList1] = *currentIt;
                auto& [albumName2, trackList2] = *currentIt2;

                if (trackList1.size() == trackList2.size())
                {
                    bool bPotentialSimilar = true;
                    for (int i = 0; i < trackList1.size(); i++)
                    {
                        auto& [trackName1, size1, mediaInfo1, mediaInfoString2] = trackList1[i];
                        auto& [trackName2, size2, mediaInfo2, mediaInfoString1] = trackList2[i];


                        auto minSize = (std::min)(mediaInfo1.duration, mediaInfo2.duration);
                        auto maxSize = (std::max)(mediaInfo1.duration, mediaInfo2.duration);

                        long long diffPercentage = (long)100 * (maxSize - minSize) / maxSize;

                        if (diffPercentage > sizeMatchPercentageThreshold)
                        {
                            bPotentialSimilar = false;
                        }
                    }

                    if (bPotentialSimilar)
                    {
                        duplicatedAlbumList.push_back({ albumName1.path().generic_wstring(), albumName2.path().generic_wstring() });
                    }
                }
            }
            currentIt++;
        }
    }

    return duplicatedAlbumList;
}




//--------------------DB



#include "SQLite/sqlite-amalgamation/sqlite3.h"



bool AlbumCollection::SaveMediaInfoDocumentToDB(std::filesystem::path path)
{
    const std::string dbPath{ path.generic_string() };

    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return rc;
    }

    // Execute SQL statements

    rc = sqlite3_exec(db, "DROP TABLE IF EXISTS AlbumListA;", 0, 0, 0);

    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS AlbumListA ("
        "ID INTEGER PRIMARY KEY, "
        "album_name TEXT, "
        "filename TEXT, "
        "format_name TEXT, "
        "format_long_name TEXT, "
        "codec_type TEXT, "
        "start_time TEXT, "
        "duration INTEGER, "
        "size TEXT, "
        "bit_rate TEXT, "
        "probe_score INTEGER, "
        "album TEXT, "
        "artist TEXT, "
        "album_artist TEXT, "
        "comment TEXT, "
        "genre TEXT, "
        "publisher TEXT, "
        "title TEXT, "
        "track TEXT, "
        "date TEXT); ", 0, 0, 0);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return rc;
    }


    for (auto [dirPath, trackList] : _AlbumList)
    {
      
        for (auto& [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::wstring albumPath = dirPath.path().wstring();
            //std::string albumPath2{ dirPath.path().generic_string()};
            //auto trackName2 = trackName.generic_string();

            std::string queryString = std::format(
                "INSERT OR REPLACE INTO AlbumListA VALUES (null, \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\");",
                CommonUtils::wstringToUtf8(albumPath),
                CommonUtils::wstringToUtf8(trackName.wstring()),

                mediaInfo.format_name,
                mediaInfo.format_long_name,
                mediaInfo.codec_type,
                mediaInfo.start_time,
                mediaInfo.duration,
                mediaInfo.size,
                mediaInfo.bit_rate,
                mediaInfo.probe_score,
                CommonUtils::wstringToUtf8(mediaInfo.tags.album),
                CommonUtils::wstringToUtf8(mediaInfo.tags.artist),
                CommonUtils::wstringToUtf8(mediaInfo.tags.album_artist),
                CommonUtils::wstringToUtf8(mediaInfo.tags.comment),
                CommonUtils::wstringToUtf8(mediaInfo.tags.genre),
                CommonUtils::wstringToUtf8(mediaInfo.tags.publisher),
                CommonUtils::wstringToUtf8(mediaInfo.tags.title),
                CommonUtils::wstringToUtf8(mediaInfo.tags.track),
                CommonUtils::wstringToUtf8(mediaInfo.tags.date));


            char* error_report;
            rc = sqlite3_exec(db, queryString.c_str(), 0, 0, &error_report);
            if (rc)
            {
                std::cerr << "***ERROR - Database error: " << error_report << std::endl;
            }
        }
    }


    if (rc != SQLITE_OK) {
        std::cerr << "Cannot insert data: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return rc;
    }


    sqlite3_close(db);


    return 0;


    //const std::string dbPath{ path.generic_string() };

    //sqlite3* db;
    //int rc = sqlite3_open(dbPath.c_str(), &db);

    //if (rc != SQLITE_OK) {
    //    std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
    //    return rc;
    //}

    //// Execute SQL statements
    //rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY, name TEXT, age INTEGER);", 0, 0, 0);

    //if (rc != SQLITE_OK) {
    //    std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
    //    sqlite3_close(db);
    //    return rc;
    //}

    //// Insert data
    //rc = sqlite3_exec(db, "INSERT INTO test VALUES (1, 'John', 25);", 0, 0, 0);

    //if (rc != SQLITE_OK) {
    //    std::cerr << "Cannot insert data: " << sqlite3_errmsg(db) << std::endl;
    //    sqlite3_close(db);
    //    return rc;
    //}

    //// Query data
    //sqlite3_stmt* stmt;
    //rc = sqlite3_prepare_v2(db, "SELECT id, name, age FROM test;", -1, &stmt, 0);

    //while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    //    int id = sqlite3_column_int(stmt, 0);
    //    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    //    int age = sqlite3_column_int(stmt, 2);

    //    std::cout << "ID: " << id << ", Name: " << name << ", Age: " << age << std::endl;
    //}

    //sqlite3_finalize(stmt);
    //sqlite3_close(db);

    return false;
}
