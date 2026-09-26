#pragma once
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// One submitting thread per renderer; tasks retain the original chunk indices.
// Owned by the renderer so workers are joined before its project DLL unloads.
class PersistentWorkers {
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    std::vector<std::thread> workers_;
    std::function<void(int,int,int)> function_;
    unsigned generation_ = 0, active_ = 0, pending_ = 0;
    int count_ = 0, per_ = 0;
    bool stopping_ = false;
    std::exception_ptr error_;
    void worker(unsigned index, unsigned seen) {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
            if (stopping_) return;
            seen = generation_;
            if (index >= active_) continue;
            const int lo = static_cast<int>(index) * per_;
            const int hi = std::min(count_, lo + per_);
            lock.unlock();
            try { if (lo < hi) function_(lo, hi, static_cast<int>(index)); }
            catch (...) { lock.lock(); if (!error_) error_ = std::current_exception(); lock.unlock(); }
            lock.lock();
            if (--pending_ == 0) done_.notify_one();
        }
    }
public:
    ~PersistentWorkers() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        wake_.notify_all();
        for (auto& thread : workers_) thread.join();
    }
    void run(int count, unsigned requested, const std::function<void(int,int,int)>& function) {
        if (count <= 0) return;
        const unsigned threads = std::max(1u, std::min(requested, static_cast<unsigned>(count)));
        if (threads == 1) { function(0, count, 0); return; }
        while (workers_.size() < threads - 1) {
            const unsigned index = static_cast<unsigned>(workers_.size()) + 1;
            workers_.emplace_back([this,index,seen=generation_] { worker(index, seen); });
        }
        const int per = (count + static_cast<int>(threads) - 1) / static_cast<int>(threads);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            function_ = function; count_ = count; per_ = per;
            active_ = threads; pending_ = threads - 1; error_ = nullptr; ++generation_;
        }
        wake_.notify_all();
        try { function(0, std::min(count, per), 0); }
        catch (...) { std::lock_guard<std::mutex> lock(mutex_); if (!error_) error_ = std::current_exception(); }
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0; });
        function_ = {};
        if (error_) std::rethrow_exception(error_);
    }
};
