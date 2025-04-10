
#include "PlatformUtils.h"




#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#endif

namespace PlatformUtils {


    void OpenDirectoryInExplorer(std::wstring dirName)
    {
        auto pathSeperator = std::filesystem::path::preferred_separator;
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



    void OpenDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
        std::wstring wPath = path.wstring();
        HINSTANCE result = ShellExecuteW(
            nullptr, L"open", wPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL
        );
        if (reinterpret_cast<intptr_t>(result) <= 32) {
            throw std::runtime_error("Failed to open directory on Windows: " + path.string());
        }
#else
        std::string p = path.string();
        // Try xdg-open first
        std::string command = "xdg-open \"" + p + "\"";
        if (std::system(command.c_str()) != 0) {
            // Fallback to common file managers
            command = "nautilus \"" + p + "\" 2>/dev/null || dolphin \"" + p + "\" 2>/dev/null";
            if (std::system(command.c_str()) != 0) {
                throw std::runtime_error("Failed to open directory on Linux: " + p);
            }
        }
#endif
    }

    void waitForKeyPress() {
#ifdef _WIN32
        _getch(); // Windows: Use _getch
#else
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    }
}



