// hpvr - the aircraft (.acf files) the Updater page can pick

#include "AircraftFinder.h"
#include "util/FileUtil.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

namespace hpvr::manip {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxDepth = 8;               // Aircraft/<group>/<aircraft>/... is deep enough

std::string Lower(std::string text)
{
    for (char& character : text)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return text;
}

bool IsAcf(const fs::path& path)
{
    return Lower(util::Utf8(path.extension())) == ".acf";
}

bool SkippedFolder(const fs::path& path)
{
    const std::string name = Lower(util::Utf8(path.filename()));
    return name == "liveries" || name == "plugins" || name == "objects" || name == "sounds" ||
           name == "cockpit" || name == "cockpit_3d" || name == "airfoils" || name == "weapons" ||
           (!name.empty() && name.front() == '.');
}

} // namespace

std::vector<fs::path> ListAcfInFolder(const fs::path& folder)
{
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec) && IsAcf(entry.path()))
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(), [](const fs::path& first, const fs::path& second) {
        return Lower(util::Utf8(first.filename())) < Lower(util::Utf8(second.filename()));
    });
    return files;
}

std::vector<fs::path> FindAllAcf(const fs::path& root, util::StopToken stop, std::atomic<std::size_t>* foldersSeen)
{
    std::vector<fs::path> files;
    std::error_code ec;
    const auto options = fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink;
    fs::recursive_directory_iterator it(root, options, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        if (stop.stop_requested())
            break;
        const fs::directory_entry& entry = *it;
        std::error_code entryError;
        if (entry.is_directory(entryError)) {
            if (foldersSeen)
                ++*foldersSeen;
            if (it.depth() >= kMaxDepth || SkippedFolder(entry.path()))
                it.disable_recursion_pending();
            continue;
        }
        if (IsAcf(entry.path()) && entry.is_regular_file(entryError))
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(), [](const fs::path& first, const fs::path& second) {
        return Lower(first.generic_string()) < Lower(second.generic_string());
    });
    return files;
}

AircraftFinder::~AircraftFinder()
{
    Stop();
}

void AircraftFinder::Stop()
{
    if (mThread.joinable()) {
        mThread.request_stop();
        mThread.join();
    }
    std::lock_guard lock(mMutex);
    mRunning = false;
}

void AircraftFinder::Start(const fs::path& root)
{
    Stop();
    {
        std::lock_guard lock(mMutex);
        mResult.reset();
        mRunning = true;
    }
    mFoldersSeen = 0;
    mThread = util::StopThread([this, root](util::StopToken stop) {
        std::vector<fs::path> files = FindAllAcf(root, stop, &mFoldersSeen);
        std::lock_guard lock(mMutex);
        if (!stop.stop_requested())
            mResult = std::move(files);
        mRunning = false;
    });
}

void AircraftFinder::Abort()
{
    Stop();
    std::lock_guard lock(mMutex);
    mResult.reset();
}

bool AircraftFinder::Running() const
{
    std::lock_guard lock(mMutex);
    return mRunning;
}

std::optional<std::vector<fs::path>> AircraftFinder::TakeResult()
{
    std::lock_guard lock(mMutex);
    if (mRunning || !mResult)
        return std::nullopt;
    std::optional<std::vector<fs::path>> result = std::move(mResult);
    mResult.reset();
    return result;
}

} // namespace hpvr::manip
