#pragma once
#include "MediaConvertionTask.h"


class MediaConvertionAsyncTask : public MediaConvertionTask
{
public:

	MediaConvertionAsyncTask() = delete;

	MediaConvertionAsyncTask(const fs::path sourcePath, const fs::path targetPath, const fs::path targetTMPPath) : MediaConvertionTask(sourcePath, targetPath, targetTMPPath) {}

	virtual int Run() override;
	virtual int PostRun() override;

	virtual ~MediaConvertionAsyncTask() {}

private:

	std::future<int> _asyncResult;
};

