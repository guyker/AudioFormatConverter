#include "FolderCompare.h"


#include <vector>
#include <algorithm>
#include <ranges>

namespace fs = std::filesystem;




std::vector<std::wstring> FolderCompare::GetFolderNamesList(std::filesystem::path path)
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


    // Finding common items using std::ranges::set_intersection
    //std::ranges::set_intersection( folderListA, folderListB, std::back_inserter(similarFolders), [](const auto& a, const auto& b) {
    //        // Case-insensitive comparison for wide strings
    //        std::wstring lowerA = a;
    //        std::wstring lowerB = b;
    //        std::transform(lowerA.begin(), lowerA.end(), lowerA.begin(), ::towlower);
    //        std::transform(lowerB.begin(), lowerB.end(), lowerB.begin(), ::towlower);
    //        return lowerA == lowerB;
    //    }
    //);


    return folderListB;
}
