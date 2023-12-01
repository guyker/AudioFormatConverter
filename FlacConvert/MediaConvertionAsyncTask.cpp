
#include "MediaConvertionAsyncTask.h"

#include <iostream>
#include <filesystem>
#include <array>
#include <algorithm>
#include <mutex>
#include <future>

#include <chrono>
#include <thread>

int MediaConvertionAsyncTask::Run()
{
    if (_status != -1) {
        _asyncResult = std::async(std::launch::async, [&]() { return ConvertFile(); });

        if (!_asyncResult.valid())
        {
            std::wcout << "***ERROR*** Invalid task: " << _sourcePath << std::endl;
            _status = -1;
        }
    }

    return _status;
}


int MediaConvertionAsyncTask::PostRun()
{
    if (_status != -1) {
        _status = _asyncResult.get();
        if (_status != -1) {
            RenameAndRemoveTMPFile();
        }
        else {
            std::wcout << "***Error running aync task: " << _sourcePath << std::endl;
        }
    }

    return _status;
}
