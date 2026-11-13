#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include "platform/hal/background_task.h"

void simPumpEvents() {}

struct Transfer {
    std::atomic<int> percent{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> release{false};
    bool cancelled = false;
};

static void transfer(void* context) {
    auto& state = *static_cast<Transfer*>(context);
    state.percent.store(50);
    while (!state.release.load() && !state.cancel.load()) std::this_thread::yield();
    state.cancelled = state.cancel.load();
    state.percent.store(100);
}

int main() {
    Transfer state;
    Platform::BackgroundTask task;
    assert(task.start(transfer, &state));
    assert(!task.start(transfer, &state));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (state.percent.load() != 50) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    // Simulate a UI update that remains blocked while the worker continues.
    state.release.store(true);
    while (!task.finished()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    assert(state.percent.load() == 100);
    assert(!state.cancelled);
    task.join();

    Transfer cancelled;
    assert(task.start(transfer, &cancelled));
    cancelled.cancel.store(true);
    task.join();
    assert(cancelled.cancelled);

    // Destruction must wait before the caller releases the worker's context.
    Transfer scoped;
    {
        Platform::BackgroundTask worker;
        assert(worker.start(transfer, &scoped));
        scoped.release.store(true);
    }
    assert(scoped.percent.load() == 100);
    puts("OK: worker progress, cancellation, result publication and lifetime");
}
