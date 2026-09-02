#include "RoomTemperatureManager.h"
#include "CommandManager/CommandManager.h"
#include "Board.h"
#include "interfaces/TemperatureSensor.h"
#include "JsonScope.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

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
    int64_t now = esp_timer_get_time();

    if (source_ == RoomTempSource::External)
    {
        // Same staleness rule as the sensor, on purpose: a simulator that stops
        // pushing looks exactly like a sensor that stopped answering, and the
        // control loop already knows what to do with that.
        if (!IsValid(externalUs_, now)) return false;
        celsius = externalTemp_;
        return true;
    }

    if (!IsValid(lastReadUs_, now)) return false;
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
        // manager's link logging). This watches the *effective* source, so it
        // reports a stalled simulator the same way it reports a dead sensor.
        // lastValid_ race-free here: this task is the only writer.
        float effective = 0;
        bool valid = GetRoomTemperature(effective);
        if (valid != lastValid_)
        {
            const char *which;
            {
                LOCK(mutex_);
                which = source_ == RoomTempSource::External ? "external" : "sensor";
            }
            if (valid) ESP_LOGI(TAG, "Room temp source restored (%s, %.1f C)", which, effective);
            else       ESP_LOGW(TAG, "Room temp source lost (%s, no valid value for 30 s)", which);
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

RequestError RoomTemperatureManager::Cmd_RoomTemp(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

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

    JsonObject resp(ctx.out);
    resp.field("valid", valid);
    resp.field("temp", temp);
    resp.field("ageMs", ageMs);
    return RequestError::Ok;
}

RequestError RoomTemperatureManager::Cmd_RoomExternal(CommandContext& ctx)
{
    float    temp = NAN;   // absent stays NAN, so "no temperature given" survives
    uint32_t off  = 0;
    RETURN_IF_ERROR(ctx.readArgs(
        Optional("temp", temp),
        Optional("off",  off)
    ));

    {
        LOCK(mutex_);
        if (off)
        {
            source_ = RoomTempSource::Sensor;
            externalUs_ = -1;
        }
        else if (!std::isnan(temp))
        {
            // One call both selects the source and refreshes it, so a simulator
            // needs exactly one command per tick.
            source_ = RoomTempSource::External;
            externalTemp_ = temp;
            externalUs_ = esp_timer_get_time();
        }
        // Neither given: a pure read, handled by WriteExternalStatus below.
    }

    WriteExternalStatus(ctx.out);
    return RequestError::Ok;
}

void RoomTemperatureManager::WriteExternalStatus(Stream &out)
{
    RoomTempSource source;
    float   ext, sensorTemp;
    int64_t extUs, sensorUs;
    {
        LOCK(mutex_);
        source     = source_;
        ext        = externalTemp_;
        extUs      = externalUs_;
        sensorTemp = lastTemp_;
        sensorUs   = lastReadUs_;
    }
    int64_t now = esp_timer_get_time();

    JsonObject resp(out);
    resp.field("source", source == RoomTempSource::External ? "external" : "sensor");
    resp.field("externalTemp", ext);
    resp.field("externalValid", IsValid(extUs, now));
    resp.field("externalAgeMs", extUs < 0 ? 0u : (uint32_t)((now - extUs) / 1000));
    // The real sensor keeps reading underneath, so a simulation run can still
    // see what the board thinks the room is — and watch it self-heat.
    resp.field("sensorTemp", sensorTemp);
    resp.field("sensorValid", IsValid(sensorUs, now));
}
