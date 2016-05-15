#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace qt_monkey_common
{
// implement own semaphore, because QSemaphore
// not annotated for -fsanitize=thread
class Semaphore final
{
public:
    explicit Semaphore(int n): count_(n) {}
    Semaphore(const Semaphore &) = delete;
    Semaphore &operator=(const Semaphore &) = delete;
    void acquire(int n = 1)
    {
        std::unique_lock<std::mutex> lock{mutex_};
        cv_.wait(lock, [this, n] { return count_ >= n; });
        count_ -= n;
    }
    template <class Rep, class Period>
    bool tryAcquire(int n, const std::chrono::duration<Rep, Period> &d)
    {
        std::unique_lock<std::mutex> lock{mutex_};
        const bool finished
            = cv_.wait_for(lock, d, [this, n] { return count_ >= n; });
        if (finished)
            count_ -= n;
        return finished;
    }
    void release(int n = 1)
    {
        std::lock_guard<std::mutex> lock{mutex_};
        count_ += n;
        cv_.notify_one();
    }

private:
    int count_ = 0;
    std::mutex mutex_;
    std::condition_variable cv_;
};
}
