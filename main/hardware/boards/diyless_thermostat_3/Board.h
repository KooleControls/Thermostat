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
#include "drivers/SocTemperature.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

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

    // Die temperature — diagnostics only, never a room-temperature source
    // (see drivers/SocTemperature.h for why it is not a TemperatureSensor).
    SocTemperature &GetSocTemperature() { return socTemp_; }

    // Display + touch (thermostat-ui). Handles are null if bring-up failed —
    // the app runs headless in that case.
    esp_lcd_panel_handle_t GetPanel() { return panel_.panel(); }
    esp_lcd_touch_handle_t GetTouch() { return touch_.handle(); }

    /// Backlight brightness, 0..100 %. 0 is fully off (the LED string is the
    /// board's single biggest heat source, so this doubles as a thermal lever).
    void SetBacklightPercent(uint8_t percent);
    uint8_t GetBacklightPercent() const { return backlightPercent_; }

    /// Change the RGB refresh clock. Lowering it proportionally cuts the
    /// continuous DMA/PSRAM traffic that scans the framebuffer out to the panel
    /// — the panel's *other* power draw, next to the backlight.
    ///
    /// This is the only way to idle the panel on this board: the ST7701's
    /// 3-wire SPI lines double as the STM32 OpenTherm UART (GPIO11/12), and
    /// with no disp_gpio wired the esp_lcd_st7701 driver implements
    /// esp_lcd_panel_disp_on_off() by sending DISPOFF over exactly those pins.
    /// Calling it would corrupt the OT link, so we never do.
    bool SetPanelPclk(uint32_t hz);
    uint32_t GetPanelPclk() const { return panelPclkHz_; }
    static constexpr uint32_t PanelPclkDefaultHz = BoardConfig::LCD_PIXEL_CLOCK_HZ;

    /// Hold the ST7701 in hardware reset — the closest this board gets to
    /// "display off", since the panel has no power-enable line and its command
    /// interface is gone (those pins are the OpenTherm UART now). Its gate
    /// drivers and internal supplies stop, which is the point when measuring the
    /// self-heating floor.
    ///
    /// ONE-WAY: bringing the panel back needs the init sequence, which needs the
    /// SPI pins we no longer have. Only a reboot restores the display.
    void HoldPanelInReset();
    bool IsPanelInReset() const { return panelInReset_; }

private:
    ServiceProvider &serviceProvider_;
    InitState initState_;

    // Hardware instances — buses first, then the drivers that use them.
    i2c_master_bus_handle_t i2cBus_ = nullptr;
    Aht20Sensor ambientSensor_;
    St7701Panel panel_;
    Gt911Touch touch_;
    Stm32OpenThermLink otLink_;
    SocTemperature socTemp_;

    uint8_t backlightPercent_ = 0;
    uint32_t panelPclkHz_ = PanelPclkDefaultHz;
    bool panelInReset_ = false;

    static constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
    static constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
    static constexpr ledc_timer_bit_t kBacklightResolution = LEDC_TIMER_10_BIT;
    static constexpr uint32_t kBacklightMaxDuty = (1u << 10) - 1;
};
