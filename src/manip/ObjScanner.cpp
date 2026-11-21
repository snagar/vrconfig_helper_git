// hpvr - reads the aircraft's OBJ files in a background thread

#include "ObjScanner.h"

#include <utility>

namespace hpvr::manip {

ObjScanner::~ObjScanner()
{
    Stop();
}

void ObjScanner::Stop()
{
    if (mThread.joinable()) {
        mThread.request_stop();
        mThread.join();
    }
    std::lock_guard lock(mMutex);
    mRunning = false;
}

std::uint64_t ObjScanner::Start(const std::filesystem::path& acfFile, const ManipTypeSet& types)
{
    Stop();
    {
        std::lock_guard lock(mMutex);
        mResult.reset();
        mRunning = true;
    }
    mProgress.filesTotal = 0;
    mProgress.filesDone  = 0;
    mProgress.bytesTotal = 0;
    mProgress.bytesDone  = 0;
    ++mScanId;

    // Everything the thread uses is copied into it.
    mThread = util::StopThread([this, acfFile, types](util::StopToken stop) {
        AircraftScan result = ScanAircraft(acfFile, types, stop, &mProgress);
        std::lock_guard lock(mMutex);
        if (!result.aborted && !stop.stop_requested())
            mResult = std::move(result);
        mRunning = false;
    });
    return mScanId;
}

void ObjScanner::Abort()
{
    Stop();
    std::lock_guard lock(mMutex);
    mResult.reset();
}

bool ObjScanner::Running() const
{
    std::lock_guard lock(mMutex);
    return mRunning;
}

float ObjScanner::Fraction() const
{
    const auto total = mProgress.bytesTotal.load();
    if (total == 0)
        return 0.0f;
    return static_cast<float>(static_cast<double>(mProgress.bytesDone.load()) / static_cast<double>(total));
}

std::optional<AircraftScan> ObjScanner::TakeResult()
{
    std::lock_guard lock(mMutex);
    if (mRunning || !mResult)
        return std::nullopt;
    std::optional<AircraftScan> result = std::move(mResult);
    mResult.reset();
    return result;
}

} // namespace hpvr::manip
