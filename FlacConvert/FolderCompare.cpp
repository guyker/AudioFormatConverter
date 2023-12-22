#include "FolderCompare.h"

#include <windows.h> 

#include <vector>
#include <algorithm>
#include <ranges>
#include <functional>


#include <forward_list>
#include <iterator>
#include <vector>


void FolderCompare::OpenDirectoryInExplorer(std::wstring dirName)
{
        
        //using namespace std::string_literals;
        //COMMAND

        //std::wstring cmdExecNameW{ L"ffmpeg" };
        //std::wstring convertParamsW{ L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats"s };
        //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + LR"( ")"s + _targetTMPPath.generic_wstring() + LR"(")"s };

        auto dirNameConv1 = dirName.substr(4, dirName.length() - 4);
        std::wstring dirNameConv2{};
        for (auto c : dirNameConv1)
        {
            if (c == '/')
            {
                dirNameConv2 += '\\';
            }
            else
            {
                dirNameConv2 += c;
            }
        }

        std::wstring cmdExecNameW{ L"explorer.exe /e '" };
        std::wstring commandW{ cmdExecNameW + dirNameConv2 + L"'"};


        try {
         //   _wsystem(commandW.c_str());

            ShellExecute(NULL, NULL, dirNameConv2.c_str(), NULL, NULL, SW_SHOWNORMAL);

        }
        catch (const std::exception& ex) {
        }

}
bool FolderCompare::Compare(EntryFileTuple entry1, EntryFileTuple entry2)
{
    auto dir1 = std::get<0>(entry1);
    auto dir2 = std::get<0>(entry2);
    auto list1 = std::get<1>(entry1);
    auto list2 = std::get<1>(entry2);

    //auto list1FileList = std::get<0>(list1);
    //auto list2FileLiet = std::get<0>(list2);
    //auto list1FileSize = std::get<1>(list1);
    //auto list2FileSize = std::get<1>(list2);


    auto it1 = list1.cbegin();
    auto it2 = list2.cbegin();
    bool bPotentialIdentical = true;

    if (list1.size() == list2.size() && list1.size() >= MinNumberOfFilesInFolderToCompare )
    {

        try
        {

            while (bPotentialIdentical && it1 != list1.cend())
            {

                auto dirName1 = dir1.path().generic_wstring();
                auto dirName2 = dir2.path().generic_wstring();

                auto [file1Name, fileSize1] = *it1;
                auto [file2Name, fileSize2] = *it2;

                fs::path path1{ dirName1 + L"/" + file1Name };
                fs::path path2{ dirName2 + L"/" + file2Name };


                auto path1Fixed = path1.lexically_normal().native();
                auto path2Fixed = path2.lexically_normal().native();


                //long long fileSize1 = fs::file_size(path1Fixed);
                //long long fileSize2 = fs::file_size(path2Fixed);

                //long long fileSize1 = fs::file_size(path1Fixed);
                //long long fileSize2 = fs::file_size(path2Fixed);

                //auto diff = (long)100 * std::labs(fileSize1 - fileSize2) / std::max(fileSize1, fileSize2);

                long long minSize = min(fileSize1, fileSize2);
                long long maxSize = max(fileSize1, fileSize2);
                long long diff = maxSize - minSize;
                long long muil10 = 10 * diff;
                long long muil100 = 100 * diff;
                long long result2 = muil100 / maxSize;

                long long result = (long)100 * diff / maxSize;

                //if (std::labs(fileSize1 - fileSize2) > 10000000)
                if (result > SimilarPercentageTriggerValue)
                {
                    bPotentialIdentical = false;
                }

                it1++;
                it2++;
            }
        }
        catch (std::exception ex)
        {
            int ii = 9;
            bPotentialIdentical = false;
        }
    }
    else
    {
        bPotentialIdentical = false;
    }


    return bPotentialIdentical;

}


void FolderCompare::FindDuplicationInGroup(DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt)
{
    if (firstIt != lastIt && firstIt != _fileList.end() && lastIt != _fileList.end())
    {
        auto currentIt = firstIt;
        while (currentIt != lastIt)
        {
            auto currentIt2 = currentIt;
            while (currentIt2 != lastIt)
            {
                currentIt2++;

                auto dir1 = std::get<0>(*currentIt);
                auto dir2 = std::get<0>(*currentIt2);

                auto nameXX1 = dir1.path().generic_wstring();
                auto nameXX2 = dir2.path().generic_wstring();

                //chake//
                auto bPotentialSimilar = Compare(*currentIt, *currentIt2);
                if (bPotentialSimilar)
                {

                    _SimilarDirs++;
                    _SimilarDirectories.push_back({ dir1.path().generic_wstring(), dir2.path().generic_wstring() });
                }

//                firstIt++;
            }
            currentIt++;
        }
    }
}


void FolderCompare::findDuplicates()
{
    //fs::directory_entry, std::vector<std::wstring>
    
    //find ranges in _fileList

    if (_fileList.empty())
    {
        return;
    }


    //for (auto& item : _fileList)
    //{
    //    auto dir = std::get<0>(item);
    //    auto fileList = std::get<1>(item);
    //    
    //    auto dirName = dir.path().generic_string();

    //    int i = 0;
    //}

    auto firstIt = _fileList.begin();
    auto secondIt = firstIt;
    secondIt++;

    while (firstIt != _fileList.end() && secondIt != _fileList.end())
    {
        bool bFound = false;
        auto dirEntry1 = std::get<0>(*firstIt);
        auto dirEntry2 = std::get<0>(*secondIt);

        auto fileList1 = std::get<1>(*firstIt);
        auto fileList2 = std::get<1>(*secondIt);

        auto pushedEndGroupIt = secondIt;
        int itemsInGroup{ 0 };
        auto fileList1Seize{ fileList1.size() };
        while (secondIt != _fileList.end() && fileList1.size() == fileList2.size())
        {
            pushedEndGroupIt = secondIt;
            dirEntry2 = std::get<0>(*secondIt);
            fileList2 = std::get<1>(*secondIt);
            secondIt++;
            bFound = true;
            itemsInGroup++;
        }
        
        secondIt = pushedEndGroupIt;

      //  secondIt--;

        auto firstIndex = std::ranges::distance(_fileList.cbegin(), firstIt);
        auto lastIndex = std::ranges::distance(_fileList.cbegin(), secondIt);

        if (bFound)
        {
            FindDuplicationInGroup(firstIt, secondIt);
            firstIt = secondIt;;
            secondIt++;
        }
        else
        {
            firstIt++;
            secondIt++;
        }
    }

    int iCount = _SimilarDirectories.size();
    for (auto entry : _SimilarDirectories)
    {
        auto [dir1, dir2] = entry;

        OpenDirectoryInExplorer(dir1);
        OpenDirectoryInExplorer(dir2);

        iCount--;
    }
}

void FolderCompare::sort()
{
    std::ranges::stable_sort(_fileList,
        [&](auto& a, auto& b) {

            auto dir1 = std::get<0>(a);
            auto dir2 = std::get<0>(b);
            auto list1 = std::get<1>(a);
            auto list2 = std::get<1>(b);

            if (list1.size() == list2.size())
            {
                if (list1.size() == 0)
                {
                    return true;
                }
                else {
                    if (list2.size() == 0)
                    {
                        return false;
                    }
                }
                
                return false;
                //auto it1 = list1.cbegin();
                //auto it2 = list2.cbegin();
                //bool bPotentialIdentical = true;
                //try
                //{
                //    while (bPotentialIdentical && it1 != list1.cend())
                //    {

                //        auto dirName1 = dir1.path().generic_wstring();
                //        auto dirName2 = dir2.path().generic_wstring();

                //        fs::path path1{ dirName1 + L"/" + *it1 };
                //        fs::path path2{ dirName2 + L"/" + *it2 };


                //        auto path1Fixed = path1.lexically_normal().native();
                //        auto path2Fixed = path2.lexically_normal().native();


                //        auto fileSize1 = fs::file_size(path1Fixed);
                //        auto fileSize2 = fs::file_size(path2Fixed);


                //        //auto diff = (long)100 * std::labs(fileSize1 - fileSize2) / std::max(fileSize1, fileSize2);
                //        auto diff = (long)100 * std::labs(fileSize1 - fileSize2) / max(fileSize1, fileSize2);

                //        //if (std::labs(fileSize1 - fileSize2) > 10000000)
                //        if (diff > 10)
                //        {
                //            bPotentialIdentical = false;
                //        }

                //        it1++;
                //        it2++;
                //    }
                //}
                //catch (std::exception ex)
                //{
                //    int ii = 9;
                //    bPotentialIdentical = false;
                //}

                //if (bPotentialIdentical)
                //{
                //    int i = 0;
                //    _SimilarDirs++;

                //    _SimilarDirectories.push_back({ dir1.path().generic_wstring(), dir2.path().generic_wstring() });
                //}

                //return bPotentialIdentical;
            }
            

            return list1.size() < list2.size();
        });
}


FileList FolderCompare::GetFolderNamesList2(std::filesystem::path path, int depth)
{
    FileList folderList;

    if (depth == 0)
    {
        return folderList;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
        auto folderName = entry.path().filename();
//        auto path = entry.path();
        auto name = folderName.generic_wstring();
        if (entry.is_directory()) {
            //auto folderName = entry.path().stem();
            //folderList.push_back(name);

            auto directoryfileList = GetFolderNamesList2(entry.path(), depth - 1);
            if (directoryfileList.size() > 0)
            {
                
                _fileList.push_back({ entry, directoryfileList });
            }
        }
        else {
            if (entry.is_regular_file())
            {
                auto hasExtension = entry.path().has_extension();
                auto fileEextension = entry.path().extension();
                std::wstring entryPath{ entry.path().wstring() };
                if (entry.path().has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {

                    //auto [file1Name, fileSize1] = *it1;
                    //auto [file2Name, fileSize2] = *it2;

             //       fs::path path1{ dirName1 + L"/" + file1Name };


                    //auto path1Fixed = path1.lexically_normal().native();
                    auto path2Fixed = entry.path().lexically_normal().native();
                    long long fileSize = fs::file_size(path2Fixed);


                    folderList.push_back({ name, fileSize });
                }
            }
        }
    }

    return folderList;
}

//            folderList.push_back({ entry, { }});

std::vector<std::wstring> FolderCompare::GetFolderNamesList(std::filesystem::path path, bool recrusive)
{
    std::vector<std::wstring> folderList;
    
    for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
        if (entry.is_directory()) {
            auto path = entry.path();
            //auto folderName = entry.path().stem();
            auto folderName = entry.path().filename();
            std::wstring name = folderName.generic_wstring();
            folderList.push_back(name);
        }
    }

    return folderList;
}


std::vector<std::wstring> FolderCompare::Compare(std::filesystem::path pathA, std::filesystem::path pathB)
{
    std::vector<std::wstring> folderListA = GetFolderNamesList(pathA);
    std::vector<std::wstring> folderListB = GetFolderNamesList(pathB);;
    std::ranges::sort(folderListA);
    std::ranges::sort(folderListB);

    std::vector<std::wstring> nonSimilarFolders;
    std::vector<std::wstring> similarFolders;

    std::ranges::set_difference(folderListA, folderListB, std::back_inserter(nonSimilarFolders));

    std::ranges::set_intersection(folderListA, folderListB, std::back_inserter(similarFolders));

    return folderListB;
}
