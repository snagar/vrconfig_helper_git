// hpvr - reads the aircraft's OBJ files in a background thread
//
// The thread only reads files (ScanAircraft: the .acf object list, then each
// OBJ file): no X-Plane, ImGui or log calls. The page starts a scan, draws Progress() each frame, and takes the
// result when it is done (TakeResult, drawing thread). Abort() stops it within
// a few thousand lines; the partial result is thrown away. Starting a new scan
// stops the previous one first. The destructor stops and joins the thread, so
// nothing outlives the window.

#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "manip/ManipTypes.h"
#include "manip/ObjManips.h"

namespace hpvr::manip {

class ObjScanner {
public:
    ObjScanner() = default;
    ~ObjScanner();

    ObjScanner(const ObjScanner&)            = delete;
    ObjScanner& operator=(const ObjScanner&) = delete;

    // Starts reading the objects of `acfFile` (stops a running scan). Returns its number.
    std::uint64_t Start(const std::filesystem::path& acfFile, const ManipTypeSet& types);
    // Requests the running scan to stop; its result is discarded.
    void Abort();

    bool Running() const;
    // 0..1 by bytes read; files done / total.
    float Fraction() const;
    std::size_t FilesDone() const  { return mProgress.filesDone; }
    std::size_t FilesTotal() const { return mProgress.filesTotal; }

    // The finished scan's result, once (empty while running / when aborted).
    std::optional<AircraftScan> TakeResult();

private:
    void Stop();                    // request stop + join

    util::StopThread                 mThread;
    ScanProgress                 mProgress;
    mutable std::mutex           mMutex;
    std::optional<AircraftScan>  mResult;        // set by the thread when done
    bool                         mRunning = false;
    std::uint64_t                mScanId = 0;
};

} // namespace hpvr::manip
