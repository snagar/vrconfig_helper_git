// hpvr - the aircraft (.acf files) the Updater page can pick
//
// ListAcfInFolder: the .acf files of one aircraft folder (its variants).
// AircraftFinder: every .acf below X-Plane's Aircraft folder, found in a
// background thread (the folder can hold thousands of files). The thread only
// reads folder entries: no X-Plane, ImGui or log calls. Abort() stops it; the
// destructor stops and joins it.

#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace hpvr::manip {

// .acf files directly in `folder`, sorted by name (letter case ignored).
std::vector<std::filesystem::path> ListAcfInFolder(const std::filesystem::path& folder);

// Every .acf below `root`, sorted by path; liveries, plugins and objects folders
// are skipped. Stops early (partial result) when `stop` is requested.
std::vector<std::filesystem::path> FindAllAcf(const std::filesystem::path& root, std::stop_token stop = {},
                                              std::atomic<std::size_t>* foldersSeen = nullptr);

class AircraftFinder {
public:
    AircraftFinder() = default;
    ~AircraftFinder();

    AircraftFinder(const AircraftFinder&)            = delete;
    AircraftFinder& operator=(const AircraftFinder&) = delete;

    void Start(const std::filesystem::path& root);
    void Abort();
    bool Running() const;
    std::size_t FoldersSeen() const { return mFoldersSeen; }
    // The finished search's result, once (empty while running / when aborted).
    std::optional<std::vector<std::filesystem::path>> TakeResult();

private:
    void Stop();

    std::jthread                                      mThread;
    std::atomic<std::size_t>                          mFoldersSeen{0};
    mutable std::mutex                                mMutex;
    std::optional<std::vector<std::filesystem::path>> mResult;
    bool                                              mRunning = false;
};

} // namespace hpvr::manip
