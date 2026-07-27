#pragma once

#include "NetworkManager/NetworkManager.h"
#include "Task.h"
#include <atomic>

// Runs a WiFi scan off the LVGL task.
//
// esp_wifi_scan_start() blocks for a few seconds; doing that from the LVGL task
// would freeze the panel and starve the render loop. So a one-shot worker fills
// the buffer, publishes Done, and the screen collects the results from its own
// refresh timer. The worker never touches LVGL.
class WifiScanner
{
public:
    static constexpr int MaxResults = 16;

    enum class State
    {
        Idle,
        Scanning,
        Done,      // finished — Results()/Count() are valid (Count() may be 0)
        Failed,    // the worker could not be started
    };

    void Init(NetworkManager& network);

    /// Kick a scan. False when one is already in flight.
    bool Start();

    State GetState() const { return state_.load(); }

    // Valid once the state is Done: the worker has finished writing by then.
    const WiFiInterface::ScanResult* Results() const { return results_; }
    int Count() const { return count_; }

private:
    NetworkManager* network_ = nullptr;
    Task task_;
    std::atomic<State> state_{State::Idle};
    WiFiInterface::ScanResult results_[MaxResults] = {};
    int count_ = 0;
};
