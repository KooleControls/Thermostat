#pragma once

#include "ServiceProvider.h"
#include "rtos.h"
#include "AmbientSensor.h"   // board-provided room temperature/humidity sensor HAL
#include <cstring>
#include <cstdint>

// Mode values mirror the gateway's HvacMode; the on-wire byte (see
// KCThermoBleProtocol.h) is this enum's underlying value.
enum class ClimateMode
{
    Off,
    Heating,
    Cooling,
    Auto,
};

constexpr const char *ClimateModeToString(ClimateMode mode)
{
    switch (mode)
    {
    case ClimateMode::Off:     return "Off";
    case ClimateMode::Heating: return "Heating";
    case ClimateMode::Cooling: return "Cooling";
    case ClimateMode::Auto:    return "Auto";
    }
    return "Off";
}

struct ClimateState
{
    float roomTemp = 20.0f;
    float roomHumidity = -1.0f;  // %RH, or <0 when the board has no humidity sensor
    float setpoint = 20.0f;
    ClimateMode mode = ClimateMode::Heating;
    bool heating = false;     // gateway-reported heat output (legacy)
    uint8_t activity = 0;     // KC_ACTIVITY_* — live HVAC state for the status icon
    bool linked = false;      // connected and gateway has confirmed state
};

// Holds the thermostat's view of the climate state. This device is a dumb
// display: the gateway owns all thermostat logic. User input is applied
// locally right away (optimistic echo, so the UI feels instant) and
// forwarded to the gateway as an intent over BLE; the gateway's control
// write confirms or corrects it. See KCThermoBleProtocol.h for the wire
// contract and BleManager for the transport.
class ClimateManager
{
    static constexpr const char *TAG = "ClimateManager";
    static constexpr float SetpointMin = 5.0f;
    static constexpr float SetpointMax = 35.0f;
    static constexpr int PollPeriodMs = 2000;
    static constexpr int PublishEveryNthPoll = 5;  // ~10 s heartbeat
    static constexpr float PublishDelta = 0.1f;    // publish sooner on change

public:
    explicit ClimateManager(ServiceProvider &serviceProvider);

    ClimateManager(const ClimateManager &) = delete;
    ClimateManager &operator=(const ClimateManager &) = delete;

    void Init();

    ClimateState GetState() const;

    // User input from the UI (LVGL task).
    void AdjustSetpoint(float delta);
    void SetMode(ClimateMode mode);

private:
    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable RecursiveMutex mutex_;
    Timer pollTimer_;

    AmbientSensor sensor_;     // board-provided HAL (internal die sensor, AHT20, …)
    float tempOffset_ = 0.0f;  // DefaultOffsetC() + user NVS fine-offset

    ClimateState state_;
    bool gatewayConfirmed_ = false;  // a control write has been received
    float lastPublishedTemp_ = -1000.0f;
    int pollsSincePublish_ = 0;

    void PollSensor();
    void PublishRoomTemp();
};
