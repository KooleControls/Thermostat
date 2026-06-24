#pragma once

#include "BoardConfig.h"
#include "I2cBus.h"
#include "Expander.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_err.h"

// ──────────────────────────────────────────────────────────────
// GT911 capacitive touch (I2C) for the Waveshare ESP32-S3-Touch-LCD-4.
// Shares the I2C bus with the CH32V003 expander; its reset line hangs off that
// expander and is pulsed by Display::Init()'s power-on/reset sequence, which
// DisplayManager runs before InitTouch(). The I2C address is auto-probed
// (0x5D default, 0x14 backup), matching the BSP.
//
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

        // Make sure the expander exists (it owns the touch reset line, pulsed
        // during Display::Init()).
        BoardExpander();

        // Probe the two possible GT911 addresses.
        uint32_t addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
        if (i2c_master_probe(bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100) != ESP_OK)
        {
            if (i2c_master_probe(bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 100) == ESP_OK)
                addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
            else
            {
                ESP_LOGW(TAG, "GT911 not found on 0x5D/0x14");
                return false;
            }
        }
        ESP_LOGI(TAG, "GT911 found @ 0x%02X", (unsigned)addr);

        // Build the GT911 panel-IO config explicitly. The library's
        // ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() macro uses C designated
        // initializers whose order doesn't match the struct in C++.
        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
        tp_io_cfg.dev_addr = addr;
        tp_io_cfg.scl_speed_hz = 400000;
        tp_io_cfg.control_phase_bytes = 1;
        tp_io_cfg.dc_bit_offset = 0;
        tp_io_cfg.lcd_cmd_bits = 16;
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
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;  // reset is on the expander
        tp_cfg.int_gpio_num = GPIO_NUM_NC;  // INT not wired to a GPIO

        err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &touch_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "GT911 init failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    esp_lcd_touch_handle_t handle() const { return touch_; }
    bool ok() const { return touch_ != nullptr; }

private:
    esp_lcd_touch_handle_t touch_ = nullptr;
};
