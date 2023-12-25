
#include "MediaConvertionTask.h"

#include <string>

namespace fs = std::filesystem;

int MediaConvertionTask::ConvertFile()
{
    if (_status != -1) {
        using namespace std::string_literals;
        //COMMAND
        //std::string convertParams{ "-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats" };
        //std::string cmdExecName{ "ffmpeg" };
        //std::string command{ cmdExecName + R"( -i ")"s + _sourcePath.generic_string() + R"(" )"s + convertParams + R"( ")"s + _targetPath.generic_string() + R"(")"s };

        std::wstring cmdExecNameW{ L"ffmpeg" };
        std::wstring convertParamsW{ L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats"s };
        std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + LR"( ")"s + _targetTMPPath.generic_wstring() + LR"(")"s };
        //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + L"'" + _targetTMPPath.generic_wstring() + L"'" };


        try {
            std::wstring sourcePath { _sourcePath };
            std::wcout << L"Processing: " << sourcePath << std::endl;
            _status = _wsystem(commandW.c_str());

        }
        catch (const std::exception& ex) {
            _status = -1;
            std::wcout << " ### COMMAND EXCEOTION :" << _sourcePath << std::endl << ex.what() << std::endl;
        }

        if (_status == -1) {
            std::wcout << " ### Error processing:" << _sourcePath << std::endl;
        }
    }

    int* saasdasd = nullptr;
     
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
        _status = RenameAndRemoveTMPFile();
    }

    return _status;
}

int MediaConvertionTask::RenameAndRemoveTMPFile()
{
    if (_status != -1) {
        fs::path sourcePath{ _sourcePath };
        auto path1Fixed = sourcePath.lexically_normal().native();
        if (!fs::exists(path1Fixed)) {
            std::wcout << L"***Error rename: source does not exist: " << _sourcePath << std::endl;
            _status = -1;
        }
        else if (!fs::exists(_targetTMPPath)) {
            std::wcout << L"***Error rename: target(tmp) does not exist: " << _targetPath << std::endl;
            _status = -1;
        } 
        else {
            std::error_code ec;
            if (fs::remove(_sourcePath, ec)) {
                fs::rename(_targetTMPPath, _targetPath);
            }
            else {
                auto errormessage { ec.message() };
                std::wcout << "***Error delete: " << _sourcePath << ", " << ec.value() << L": " << std::endl;// std::wstring(ec.message()) << std::endl;
                //fs::remove(completedFiles.second, ec);
                _status = -1;
            }
        }
    }

    return _status;
}
