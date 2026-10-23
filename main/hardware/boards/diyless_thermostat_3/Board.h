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
// no Led role. Its one button is the BOOT strap (BootButtonPressed). Display/touch/OT-link drivers arrive with their feature
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

    /// Brings up the backlight PWM (at 0 %) and the panel, and nothing else, so
    /// the boot logo can go up before the slow part of Init() — the OpenTherm
    /// co-processor's warm-up alone is ~0.9 s. Init() calls it if nobody has.
    void InitDisplay();

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

    /// Undo a sideways shift of the picture at the next VSYNC; harmless when
    /// there is none. See St7701Panel::RestartDma.
    void ResyncPanel() { panel_.RestartDma(); }

    /// The size of one pixel on the panel, in nm. The two differ on this board:
    /// see LCD_ACTIVE_W_UM in BoardConfig.h.
    static constexpr int32_t GetPixelPitchXNm() { return BoardConfig::LCD_ACTIVE_W_UM * 1000 / BoardConfig::LCD_H_RES; }
    static constexpr int32_t GetPixelPitchYNm() { return BoardConfig::LCD_ACTIVE_H_UM * 1000 / BoardConfig::LCD_V_RES; }

    /// Backlight brightness, 0..100 %. 0 is fully off (the LED string is the
    /// board's single biggest heat source, so this doubles as a thermal lever).
    void SetBacklightPercent(uint8_t percent);
    uint8_t GetBacklightPercent() const { return backlightPercent_; }

    /// Whether the BOOT button on the back is held right now. Takes ~10 µs
    /// with interrupts off on the calling core, so poll it, but not in a loop.
    bool BootButtonPressed();


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
    bool displayInited_ = false;

    static constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
    static constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
    static constexpr ledc_timer_bit_t kBacklightResolution = LEDC_TIMER_10_BIT;
    static constexpr uint32_t kBacklightMaxDuty = (1u << 10) - 1;

    // How long GPIO0 is let go before it is read: a few time constants of the
    // pull-up against the pin and the panel input it shares.
    static constexpr uint32_t kBootButtonSettleUs = 10;
};
