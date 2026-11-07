//
// Where the engine's work runs. Mail stores block on the network, so
// the window posts jobs to a worker and gets results back on its own
// thread. The engine sees only this interface: tests and the headless
// program use inline_executor and run everything in one thread.
//
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef APP_EXECUTOR_H
#define APP_EXECUTOR_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>

namespace app {

// A place for jobs, and a way back.
class executor {
public:
    virtual ~executor() = default;

    // Run a job away from the interface thread, in submission order.
    virtual void run(std::function<void()> job) = 0;

    // Run a result on the interface thread.
    virtual void deliver(std::function<void()> result) = 0;
};

// Runs everything immediately, on the calling thread.
class inline_executor final : public executor {
public:
    void run(std::function<void()> job) override { job(); }
    void deliver(std::function<void()> result) override { result(); }
};

// One worker thread. Jobs keep their order, so a store is only ever
// touched by one thread and needs no locks of its own.
class worker_executor final : public executor {
public:
    //
    // Start the worker.
    //
    // Parameters:
    //      post        - Queues a callable on the interface thread.
    //
    explicit worker_executor(
        std::function<void(std::function<void()>)> post);

    // Finish the running job, drop those waiting, and stop.
    ~worker_executor() override;

    void run(std::function<void()> job) override;
    void deliver(std::function<void()> result) override;

    // Run a final job, then wait for the queue to drain.
    void drain();

private:
    void loop(std::stop_token stop);

    std::function<void(std::function<void()>)> post_;
    std::mutex lock_;
    std::condition_variable_any wake_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> jobs_;
    bool busy_ = false;
    std::jthread thread_;
};

} // namespace app

#endif
