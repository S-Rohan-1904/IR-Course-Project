#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    std::size_t size() const { return workers_.size(); }

    template <typename Task>
    auto submit(Task task) -> std::future<decltype(task())> {
        using Result = decltype(task());

        auto packaged = std::make_shared<std::packaged_task<Result()>>(std::move(task));
        std::future<Result> future = packaged->get_future();

        {
            const std::lock_guard<std::mutex> lock(mutex_);
            queue_.push([packaged] { (*packaged)(); });
        }
        available_.notify_one();

        return future;
    }

private:
    void run_worker();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable available_;
    bool stopping_ = false;
};
