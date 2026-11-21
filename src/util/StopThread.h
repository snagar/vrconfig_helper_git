// hpvr - a background thread that can be asked to stop
//
// Does what util::StopThread / util::StopToken do for this plugin, using only
// std::thread and std::atomic: Apple's libc++ long marked jthread as
// experimental, so the macOS build would depend on the Xcode version.
//   - The function gets a StopToken; it checks token.stop_requested().
//   - request_stop() sets the flag; the destructor and assigning a new thread
//     request stop and wait for the old thread first (as util::StopThread does).
//   - A default StopToken never reports a stop (for calls on the main thread).

#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace hpvr::util {

class StopToken {
public:
    StopToken() = default;
    explicit StopToken(std::shared_ptr<const std::atomic<bool>> flag) : mFlag(std::move(flag)) {}

    bool stop_requested() const noexcept { return mFlag && mFlag->load(std::memory_order_relaxed); }

private:
    std::shared_ptr<const std::atomic<bool>> mFlag;
};

class StopThread {
public:
    StopThread() = default;

    template <typename Function>
    explicit StopThread(Function&& function)
        : mFlag(std::make_shared<std::atomic<bool>>(false))
    {
        mThread = std::thread([work = std::forward<Function>(function), token = StopToken(mFlag)]() mutable {
            work(token);
        });
    }

    ~StopThread() { StopAndJoin(); }

    StopThread(const StopThread&)            = delete;
    StopThread& operator=(const StopThread&) = delete;
    StopThread(StopThread&&) noexcept        = default;

    StopThread& operator=(StopThread&& other) noexcept
    {
        if (this != &other) {
            StopAndJoin();
            mThread = std::move(other.mThread);
            mFlag   = std::move(other.mFlag);
        }
        return *this;
    }

    bool joinable() const noexcept { return mThread.joinable(); }
    void join() { mThread.join(); }
    void request_stop() noexcept
    {
        if (mFlag)
            mFlag->store(true, std::memory_order_relaxed);
    }

private:
    void StopAndJoin()
    {
        request_stop();
        if (mThread.joinable())
            mThread.join();
    }

    std::thread                        mThread;
    std::shared_ptr<std::atomic<bool>> mFlag;
};

} // namespace hpvr::util
