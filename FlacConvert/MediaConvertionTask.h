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
    MediaConvertionTask(const fs::path sourcePath, const fs::path targetPath, const fs::path targetTMPPath, bool bAsync = false) :
        _sourcePath(sourcePath), _targetPath(targetPath), _targetTMPPath(targetTMPPath), _status(0) {}

    virtual int Run();
    virtual int PostRun();

    virtual int GetStatus() { return _status; }

    virtual ~MediaConvertionTask() {}

protected:

    virtual int ConvertFile();
    virtual int RenameAndRemoveTMPFile();

    const fs::path _sourcePath;
    const fs::path _targetPath;
    const fs::path _targetTMPPath;

    int _status;
};
