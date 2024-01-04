
#include "WindowsHelpers.h"



#include <Windows.h>

void WindowsHelpers::OpenDirectoryInExplorer(std::wstring dirName)
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
    std::wstring commandW{ cmdExecNameW + dirNameConv2 + L"'" };


    try {
        //   _wsystem(commandW.c_str());

        ShellExecute(NULL, NULL, dirNameConv2.c_str(), NULL, NULL, SW_SHOWNORMAL);

    }
    catch (const std::exception& ex) {
    }

}

