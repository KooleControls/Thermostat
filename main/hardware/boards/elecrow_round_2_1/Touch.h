#pragma once

#include "BoardConfig.h"
#include "I2cBus.h"
#include "Pcf8574.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_err.h"

// ──────────────────────────────────────────────────────────────
// CST826 capacitive touch (CST816 family) for the Elecrow CrowPanel 2.1".
// Shares the I2C bus with the PCF8574; its reset/INT lines hang off that
// expander, so we pulse touch reset via the PCF before probing the controller.
// Init() is best-effort: returns false (display keeps working) if the
// controller can't be reached.
// ──────────────────────────────────────────────────────────────

class Touch
{
    static constexpr const char *TAG = "Touch";

public:
    bool Init()
    {
        i2c_master_bus_handle_t bus = BoardI2cBus();
        if (!bus) return false;

        if (BoardPcf().ok())
            BoardPcf().ResetTouch();

        // Build the panel-IO config explicitly (designated-initializer order
        // differs in C++, same caveat as the Sunton GT911 driver).
        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
        tp_io_cfg.dev_addr = BoardConfig::TOUCH_I2C_ADDR;
        tp_io_cfg.scl_speed_hz = 100000;
        tp_io_cfg.control_phase_bytes = 1;
        tp_io_cfg.dc_bit_offset = 0;
        tp_io_cfg.lcd_cmd_bits = 8;    // CST816 family uses 8-bit register addrs
        tp_io_cfg.lcd_param_bits = 0;
        tp_io_cfg.flags.disable_control_phase = 1;

        esp_err_t err = esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Touch panel IO init failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = BoardConfig::LCD_H_RES;
        tp_cfg.y_max = BoardConfig::LCD_V_RES;
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;  // reset is on the PCF8574
        tp_cfg.int_gpio_num = GPIO_NUM_NC;  // INT is on the PCF8574

        err = esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &touch_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "CST816/CST826 init failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    esp_lcd_touch_handle_t handle() const { return touch_; }
    bool ok() const { return touch_ != nullptr; }

private:
    esp_lcd_touch_handle_t touch_ = nullptr;
};
