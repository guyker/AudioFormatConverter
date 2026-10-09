#pragma once

#include <iostream>
#include <filesystem>
#include <array>
#include <algorithm>
#include <mutex>
#include <future>

#include <chrono>
#include <thread>


namespace fs = std::filesystem;


class MediaConvertionTask {
public:
    MediaConvertionTask() = delete;
    MediaConvertionTask(const fs::path sourcePath, const fs::path targetPath, const fs::path targetTMPPath, bool overwriteTarget = true) :
        _sourcePath(sourcePath), _targetPath(targetPath), _targetTMPPath(targetTMPPath), _overwriteTarget(overwriteTarget), _status(0) {}

    virtual int Run();
    virtual int PostRun();

    virtual int GetStatus() { return _status; }

    virtual ~MediaConvertionTask() {}

protected:

    virtual int ConvertFile();
    virtual int InstallTemporaryOutput();

    const fs::path _sourcePath;
    const fs::path _targetPath;
    const fs::path _targetTMPPath;
    const bool _overwriteTarget;

    int _status;
};
