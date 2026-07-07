#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "BoardConfig.h"
#include "drivers/Aht20Sensor.h"
#include "interfaces/TemperatureSensor.h"
#include "interfaces/HumiditySensor.h"
#include "driver/i2c_master.h"
#include "drivers/Stm32OpenThermLink.h"
#include "interfaces/OtLink.h"
#include "drivers/St7701Panel.h"
#include "drivers/Gt911Touch.h"
#include "driver/gpio.h"

// ──────────────────────────────────────────────────────────────
// Board for the DIYLESS OpenTherm Thermostat 3 (hw rev v3.3).
// ESP32-S3 · 8 MB flash · 8 MB octal PSRAM · 4.0" 480x480 ST7701S ·
// GT911 touch · AHT20 · STM32L051 OpenTherm co-processor.
//
// Owns the shared I2C master bus (GT911 + AHT20 sit on SDA17/SCL18) and
// every driver instance. Exposes the duck-typed capability surface the
// application compiles against. This board has no user LED and exposes
// no Led role. Display/touch/OT-link drivers arrive with their feature
// items (thermostat-ui, opentherm-link) and will be owned here too.
// ──────────────────────────────────────────────────────────────

class Board
{
    static constexpr const char *TAG = "Board";

public:
    explicit Board(ServiceProvider &serviceProvider);

    Board(const Board &) = delete;
    Board &operator=(const Board &) = delete;
    Board(Board &&) = delete;
    Board &operator=(Board &&) = delete;

    void Init();

    TemperatureSensor &GetTemperatureSensor() { return ambientSensor_; }
    HumiditySensor &GetHumiditySensor() { return ambientSensor_; }
    OtLink &GetOtLink() { return otLink_; }

    // Display + touch (thermostat-ui). Handles are null if bring-up failed —
    // the app runs headless in that case.
    esp_lcd_panel_handle_t GetPanel() { return panel_.panel(); }
    esp_lcd_touch_handle_t GetTouch() { return touch_.handle(); }
    void SetBacklight(bool on) { gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_BACKLIGHT, on ? 1 : 0); }

private:
    ServiceProvider &serviceProvider_;
    InitState initState_;

    // Hardware instances — buses first, then the drivers that use them.
    i2c_master_bus_handle_t i2cBus_ = nullptr;
    Aht20Sensor ambientSensor_;
    St7701Panel panel_;
    Gt911Touch touch_;
    Stm32OpenThermLink otLink_;
};
