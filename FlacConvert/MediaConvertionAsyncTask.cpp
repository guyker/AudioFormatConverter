
#include "MediaConvertionAsyncTask.h"

#include <iostream>
#include <filesystem>
#include <array>
#include <algorithm>
#include <mutex>
#include <future>

#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include "CommonUtils.h"

int MediaConvertionAsyncTask::Run()
{
    if (_status != -1) {
        _asyncResult = std::async(std::launch::async, [&]() { return ConvertFile(); });

        if (!_asyncResult.valid())
        {
            auto sourPath = CommonUtils::utf8string_to_string(_sourcePath.u8string());
            spdlog::error("***ERROR*** Invalid task: {}", sourPath);
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
            spdlog::error("***Error running aync task: {}", CommonUtils::utf8string_to_string(_sourcePath.u8string()));
        }
    }

    return _status;
}
