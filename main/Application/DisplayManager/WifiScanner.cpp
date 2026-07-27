#include "WifiScanner.h"
#include "esp_log.h"

static const char* TAG = "WifiScanner";

void WifiScanner::Init(NetworkManager& network)
{
    network_ = &network;
    task_.Init("wifi_scan", 5, 6144);   // esp_wifi_scan_start needs the headroom
    task_.SetHandler([this]() {
        // An empty result is a successful scan that found nothing, not an error —
        // the screen says "no networks found" rather than "scan failed".
        count_ = network_->wifi().Scan(results_, MaxResults);
        ESP_LOGI(TAG, "Scan done: %d networks", count_);
        state_.store(State::Done);
    });
}

bool WifiScanner::Start()
{
    if (state_.load() == State::Scanning) return false;

    count_ = 0;
    state_.store(State::Scanning);

    if (!task_.Run())
    {
        ESP_LOGE(TAG, "Could not start the scan task");
        state_.store(State::Failed);
        return false;
    }
    return true;
}
