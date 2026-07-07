#include "RoomTemperatureManager.h"
#include "CommandManager/CommandManager.h"
#include "Board.h"
#include "interfaces/TemperatureSensor.h"
#include "JsonScope.h"
#include "esp_log.h"
#include "esp_timer.h"

RoomTemperatureManager::RoomTemperatureManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void RoomTemperatureManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);

    task_.Init("roomtemp", 5, 4096);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (sampling every %d ms)", SampleIntervalMs);
}

bool RoomTemperatureManager::GetRoomTemperature(float &celsius)
{
    LOCK(mutex_);
    if (lastReadUs_ < 0) return false;
    if (esp_timer_get_time() - lastReadUs_ > ValidityUs) return false;
    celsius = lastTemp_;
    return true;
}

void RoomTemperatureManager::Loop()
{
    TemperatureSensor &sensor = serviceProvider_.getBoard().GetTemperatureSensor();

    while (true)
    {
        float t = 0;
        if (sensor.ReadTemperature(t))
        {
            LOCK(mutex_);
            lastTemp_ = t;
            lastReadUs_ = esp_timer_get_time();
        }

        // Log valid<->invalid transitions once (edge-detected, like the OT
        // manager's link logging). lastValid_/lastTemp_ race-free here: this
        // task is the only writer.
        float unused;
        bool valid = GetRoomTemperature(unused);
        if (valid != lastValid_)
        {
            if (valid) ESP_LOGI(TAG, "Room temp source restored (%.1f C)", lastTemp_);
            else       ESP_LOGW(TAG, "Room temp source lost (no valid sample for 30 s)");
            lastValid_ = valid;
        }

        vTaskDelay(pdMS_TO_TICKS(SampleIntervalMs));
    }
}

void RoomTemperatureManager::Cmd_RoomTemp(Stream &, Stream &out)
{
    float   temp;
    int64_t readUs;
    {
        LOCK(mutex_);
        temp   = lastTemp_;
        readUs = lastReadUs_;
    }
    int64_t now = esp_timer_get_time();
    bool     valid = readUs >= 0 && (now - readUs) <= ValidityUs;
    uint32_t ageMs = readUs < 0 ? 0 : (uint32_t)((now - readUs) / 1000);

    JsonObject resp(out);
    resp.field("valid", valid);
    resp.field("temp", temp);
    resp.field("ageMs", ageMs);
}
