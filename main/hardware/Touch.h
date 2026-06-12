#pragma once

#include "BoardConfig.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_err.h"

// ──────────────────────────────────────────────────────────────
// GT911 capacitive touch driver (I2C) for the ESP32-8048S043.
// Init() is best-effort: it returns false (and the display keeps working)
// if the controller can't be reached, rather than aborting the boot.
// ──────────────────────────────────────────────────────────────

class Touch
{
    static constexpr const char *TAG = "Touch";

public:
    bool Init()
    {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = I2C_NUM_0;
        bus_cfg.sda_io_num = (gpio_num_t)BoardConfig::TOUCH_PIN_SDA;
        bus_cfg.scl_io_num = (gpio_num_t)BoardConfig::TOUCH_PIN_SCL;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;

        i2c_master_bus_handle_t i2c_bus = nullptr;
        esp_err_t err = i2c_new_master_bus(&bus_cfg, &i2c_bus);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
            return false;
        }

        // Build the GT911 panel-IO config explicitly. The library's
        // ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() macro uses C designated
        // initializers whose order doesn't match the struct in C++.
        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
        tp_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
        tp_io_cfg.scl_speed_hz = 100000;
        tp_io_cfg.control_phase_bytes = 1;
        tp_io_cfg.dc_bit_offset = 0;
        tp_io_cfg.lcd_cmd_bits = 16;
        tp_io_cfg.flags.disable_control_phase = 1;
        err = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Touch panel IO init failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = BoardConfig::LCD_H_RES;
        tp_cfg.y_max = BoardConfig::LCD_V_RES;
        tp_cfg.rst_gpio_num = (gpio_num_t)BoardConfig::TOUCH_PIN_RST;
        tp_cfg.int_gpio_num = (gpio_num_t)BoardConfig::TOUCH_PIN_INT;
        tp_cfg.process_coordinates = &Touch::ScaleCoordinates;

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
    // The GT911 ships factory-configured for 480x272 (see BoardConfig.h);
    // scale its raw coordinates up to the 800x480 panel.
    static void ScaleCoordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                                 uint16_t *strength, uint8_t *pointNum, uint8_t maxPointNum)
    {
        for (uint8_t i = 0; i < *pointNum && i < maxPointNum; i++)
        {
            x[i] = (uint16_t)((uint32_t)x[i] * BoardConfig::LCD_H_RES / BoardConfig::TOUCH_RAW_X_MAX);
            y[i] = (uint16_t)((uint32_t)y[i] * BoardConfig::LCD_V_RES / BoardConfig::TOUCH_RAW_Y_MAX);
        }
    }

    esp_lcd_touch_handle_t touch_ = nullptr;
};
