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

bool RoomTemperatureManager::IsValid(int64_t readUs, int64_t now)
{
    return readUs >= 0 && (now - readUs) <= ValidityUs;
}

bool RoomTemperatureManager::GetRoomTemperature(float &celsius) const
{
    LOCK(mutex_);
    if (!IsValid(lastReadUs_, esp_timer_get_time())) return false;
    celsius = lastTemp_;
    return true;
}

void RoomTemperatureManager::Loop()
{
    TemperatureSensor &sensor = serviceProvider_.getBoard().GetTemperatureSensor();
    HumiditySensor &humidity = serviceProvider_.getBoard().GetHumiditySensor();

    while (true)
    {
        float t = 0;
        if (sensor.ReadTemperature(t))
        {
            LOCK(mutex_);
            lastTemp_ = t;
            lastReadUs_ = esp_timer_get_time();
        }

        // Humidity rides along on the same cadence deliberately: the AHT20 is a
        // trigger-then-latch device with internal state, so it must have exactly
        // one sampler. Consumers read the cached value instead of the sensor.
        float rh = 0;
        if (humidity.ReadHumidity(rh))
        {
            LOCK(mutex_);
            lastHumidity_ = rh;
            lastHumidityUs_ = esp_timer_get_time();
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

bool RoomTemperatureManager::GetRoomHumidity(float &percent) const
{
    LOCK(mutex_);
    if (!IsValid(lastHumidityUs_, esp_timer_get_time())) return false;
    percent = lastHumidity_;
    return true;
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
    bool     valid = IsValid(readUs, now);
    uint32_t ageMs = readUs < 0 ? 0 : (uint32_t)((now - readUs) / 1000);

    JsonObject resp(out);
    resp.field("valid", valid);
    resp.field("temp", temp);
    resp.field("ageMs", ageMs);
}
