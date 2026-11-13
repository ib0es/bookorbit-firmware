#pragma once

#include <atomic>
#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <system_error>
#include <thread>
#endif
#include <Arduino.h>

namespace Platform {

// Scoped worker: the caller must keep its context alive until join(). Only the
// caller polls input or draws; the worker owns its blocking I/O until finished.
class BackgroundTask {
public:
    BackgroundTask() = default;
    BackgroundTask(const BackgroundTask&) = delete;
    BackgroundTask& operator=(const BackgroundTask&) = delete;
    ~BackgroundTask() { join(); }

    bool start(void (*work)(void*), void* context) {
        if (_started) return false;
        _work = work;
        _context = context;
        _done.store(false, std::memory_order_relaxed);
#if defined(ESP_PLATFORM)
        // ESP-IDF specifies stack size in bytes. Leave core placement to the
        // scheduler: X4 is single-core, while Picco can run beside the UI.
        _started = xTaskCreate(entry, "book-download", 8192, this, 1, nullptr) == pdPASS;
#else
        try {
            _thread = std::thread(entry, this);
            _started = true;
        } catch (const std::system_error&) {
            _started = false;
        }
#endif
        return _started;
    }

    bool finished() const { return _done.load(std::memory_order_acquire); }

    void join() {
        if (!_started) return;
        while (!finished()) delay(10);
#if !defined(ESP_PLATFORM)
        _thread.join();
#endif
        _started = false;
    }

private:
    static void entry(void* context) {
        auto* task = static_cast<BackgroundTask*>(context);
        task->_work(task->_context);
        // Publish all results, then never access task/context again: the caller
        // may return as soon as it observes completion.
        task->_done.store(true, std::memory_order_release);
#if defined(ESP_PLATFORM)
        vTaskDelete(nullptr);
#endif
    }

    void (*_work)(void*) = nullptr;
    void* _context = nullptr;
    bool _started = false;
    std::atomic<bool> _done{false};
#if !defined(ESP_PLATFORM)
    std::thread _thread;
#endif
};

}  // namespace Platform
