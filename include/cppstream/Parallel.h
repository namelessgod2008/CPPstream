#pragma once

// The execution substrate behind parallel streams.
//
// Java runs parallel streams on the ForkJoinPool.commonPool(), which can split a
// task and let the worker that is waiting on a subtask steal other work. C++ has
// no such pool in the standard library, and this library's stages are pull-based
// closures that cannot be split at all (see Stream.h). What is needed here is
// therefore much smaller: a fixed pool plus a parallel-for that spreads the
// per-element work of one batch over the workers. `detail::ThreadPool::forEach`
// is that primitive, and it is the only thing in the library that owns threads.
//
// A nested call made from a pool worker runs inline instead of queueing, which is
// what keeps the coarse "each stage batches independently" design from deadlocking
// against itself. See the comment on workerFlag() for why that is enough.

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <latch>
#include <mutex>
#include <thread>
#include <utility>

namespace cppstream {

namespace detail {

/// The worker count a pool starts with: the hardware concurrency, or 1 when the
/// platform declines to report one so that parallel streams still terminate.
[[nodiscard]] inline std::size_t detectedParallelism() noexcept {
    const unsigned int reported = std::thread::hardware_concurrency();
    return reported == 0 ? std::size_t{1} : static_cast<std::size_t>(reported);
}

/// A fixed pool of worker threads with a blocking parallel-for.
///
/// This is deliberately not a general task scheduler: there is no future, no
/// stealing and no dynamic growth. It exists to answer exactly one question --
/// "run this indexed loop across the cores and come back when it is done" -- and
/// that is what keeps it small enough to reason about.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t workers) : active_(workers) {
        threads_.reserve(workers);
        for (std::size_t index = 0; index < workers; ++index) {
            threads_.emplace_back([this] { workerLoop(); });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool() {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (std::thread& worker : threads_) {
            worker.join();
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return threads_.size(); }

    /// How many of the pool's threads a parallel-for may use. Never below 1, so
    /// there is no "parallelism 0" state for the callers to special-case.
    [[nodiscard]] std::size_t activeWorkers() const noexcept {
        return std::min(active_.load(std::memory_order_relaxed), threads_.size());
    }

    /// Resizes the *usable* part of the pool. Threads are never created or joined
    /// at run time, so a value above size() is clamped rather than honoured; that
    /// keeps a caller from paying for a new pool every time it tunes the count.
    void setActiveWorkers(std::size_t workers) noexcept {
        active_.store(std::clamp(workers, std::size_t{1}, threads_.size()),
                      std::memory_order_relaxed);
    }

    /// Runs `fn(index)` for every index in [0, count), spread over the pool, and
    /// blocks until every index has been visited. The first exception thrown by an
    /// invocation is rethrown here once the outstanding work has drained, so a
    /// throwing stage behaves like a throwing sequential stage.
    ///
    /// Work is handed out dynamically (a shared atomic cursor) rather than split
    /// into equal static chunks: the elements of a stream are rarely uniform in
    /// cost, and a single slow element would otherwise hold up a whole chunk.
    template <class F>
    void forEach(std::size_t count, F fn) {
        if (count == 0) {
            return;
        }
        const std::size_t runners = std::min(count, activeWorkers());
        if (runners <= 1 || workerFlag()) {
            for (std::size_t index = 0; index < count; ++index) {
                fn(index);
            }
            return;
        }

        std::atomic<std::size_t> cursor{0};
        std::exception_ptr failure;
        std::mutex failureMutex;
        auto run = [&] {
            for (;;) {
                const std::size_t index = cursor.fetch_add(1, std::memory_order_relaxed);
                if (index >= count) {
                    return;
                }
                try {
                    fn(index);
                } catch (...) {
                    {
                        std::scoped_lock lock(failureMutex);
                        if (!failure) {
                            failure = std::current_exception();
                        }
                    }
                    // Park the cursor past the end so the remaining runners stop
                    // instead of starting further work behind a failed one.
                    cursor.store(count, std::memory_order_relaxed);
                    return;
                }
            }
        };

        std::latch done(static_cast<std::ptrdiff_t>(runners));
        for (std::size_t index = 0; index < runners; ++index) {
            submit([&run, &done] {
                run();
                done.count_down();
            });
        }
        done.wait();
        if (failure) {
            std::rethrow_exception(failure);
        }
    }

private:
    /// True while the calling thread is inside a task belonging to this pool.
    ///
    /// Every worker in the process belongs to the one shared pool, so this is a
    /// safe "am I already busy?" test. A parallel stage that is pulled *from* a
    /// worker -- a mapper that itself reads a parallel stream, say -- then runs its
    /// batch inline instead of submitting a second task and waiting for a thread
    /// that may never be free. Without it, the two nested waits could deadlock; with
    /// it, the inner stage silently degrades to sequential, which is the same trade
    /// ForkJoinPool makes when it cannot split.
    [[nodiscard]] static bool& workerFlag() noexcept {
        static thread_local bool flag = false;
        return flag;
    }

    void submit(std::move_only_function<void()> task) {
        {
            std::scoped_lock lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        ready_.notify_one();
    }

    void workerLoop() {
        workerFlag() = true;
        for (;;) {
            std::move_only_function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            try {
                task();
            } catch (...) {  // NOLINT(bugprone-empty-catch): swallowing is the point here
                // forEach funnels every failure back to its caller, so a task that
                // throws here is a bug in this file. Letting it escape would kill
                // the worker and wedge every later parallel stream, so the pool
                // stays alive and the bug surfaces as a missing result instead.
            }
        }
    }

    /// Declared before threads_ so that it is initialised before any worker runs.
    std::atomic<std::size_t> active_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::move_only_function<void()>> tasks_;
    bool stopping_ = false;
    std::vector<std::thread> threads_;
};

}  // namespace detail

/// The process-wide pool parallel streams run on.
///
/// Built on first use, never rebuilt: the worker count is fixed at start-up and
/// only the *usable* part of it is tunable afterwards, see setParallelism.
[[nodiscard]] inline detail::ThreadPool& parallelPool() {
    static detail::ThreadPool pool(detail::detectedParallelism());
    return pool;
}

/// Java: ForkJoinPool.commonPool().getParallelism(), in spirit. How many workers
/// parallel stages may use; 1 means "run everything sequentially".
[[nodiscard]] inline std::size_t parallelism() noexcept {
    return parallelPool().activeWorkers();
}

/// Caps how many of the shared pool's workers parallel streams may use.
///
/// Java configures this once through the `java.util.concurrent.ForkJoinPool.
/// common.parallelism` system property and the property is read when the pool is
/// first touched. Here the pool's threads are created once too, but the cap is
/// just an atomic the stages read per batch, so this may be called at any time:
/// lowering it takes effect on the next batch, and it never creates or destroys
/// threads. Values below 1 are clamped to 1, values above the pool size to the
/// pool size.
inline void setParallelism(std::size_t workers) noexcept {
    parallelPool().setActiveWorkers(workers);
}

}  // namespace cppstream
