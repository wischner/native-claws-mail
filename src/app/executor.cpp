//
// The worker thread.
//
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "app/executor.h"

#include <exception>

#include "util/log.h"

namespace app {

worker_executor::worker_executor(
    std::function<void(std::function<void()>)> post)
    : post_(std::move(post)),
      thread_([this](std::stop_token stop) { loop(stop); }) {}

worker_executor::~worker_executor() {
    thread_.request_stop();
    wake_.notify_all();
}

void worker_executor::run(std::function<void()> job) {
    {
        const std::scoped_lock hold(lock_);
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void worker_executor::deliver(std::function<void()> result) {
    post_(std::move(result));
}

void worker_executor::drain() {
    std::unique_lock hold(lock_);
    idle_.wait(hold, [this] { return jobs_.empty() && !busy_; });
}

void worker_executor::loop(std::stop_token stop) {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock hold(lock_);
            busy_ = false;
            if (jobs_.empty())
                idle_.notify_all();
            wake_.wait(hold, stop, [this] { return !jobs_.empty(); });
            if (stop.stop_requested())
                return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            busy_ = true;
        }
        try {
            job();
        } catch (const std::exception &failure) {
            // The engine reports its own; this keeps a stray one from
            // ending the process with mail half filed.
            util::log_write(util::log_level::error, failure.what());
        }
    }
}

} // namespace app
