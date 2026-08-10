#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_err.h"
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Reusable driver — GT911 capacitive touch over I2C.
//
// Bus-agnostic: the board supplies an i2c_master bus handle and the per-board
// parameters (resolution, reset/INT pins, address, scaling). The board wraps
// this in its own `Touch` class (handle()/ok()) — see a board's Touch.h.
//
// The library's ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() macro uses C designated
// initializers whose order doesn't match the struct in C++, so the panel-IO
// config is built explicitly here.
//
// Init() is best-effort: returns false (so the display keeps working) rather
// than aborting if the controller can't be reached.
// ──────────────────────────────────────────────────────────────

struct Gt911Config
{
    i2c_master_bus_handle_t bus = nullptr;
    int x_max = 0;
    int y_max = 0;
    gpio_num_t rst = GPIO_NUM_NC;
    gpio_num_t intr = GPIO_NUM_NC;
    uint32_t scl_speed_hz = 400000;
    // Trigger the INT line on both edges instead of the single edge implied by
    // esp_lcd_touch's levels.interrupt. See Init() for why a board wants this.
    bool intr_any_edge = false;
    // When true, probe 0x5D then 0x14; otherwise use fixed_addr as-is.
    bool auto_probe_addr = true;
    uint32_t fixed_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    // Optional raw→panel coordinate transform (e.g. a panel whose GT911 ships
    // configured for a different resolution). Matches esp_lcd_touch's field.
    void (*process_coordinates)(esp_lcd_touch_handle_t, uint16_t *, uint16_t *,
                                uint16_t *, uint8_t *, uint8_t) = nullptr;
};

class Gt911Touch
{
    static constexpr const char *TAG = "Touch";

public:
    bool Init(const Gt911Config &cfg)
    {
        if (!cfg.bus) return false;

        uint32_t addr = cfg.fixed_addr;
        if (cfg.auto_probe_addr)
        {
            if (i2c_master_probe(cfg.bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100) == ESP_OK)
                addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
            else if (i2c_master_probe(cfg.bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 100) == ESP_OK)
                addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
            else
            {
                ESP_LOGW(TAG, "GT911 not found on 0x5D/0x14");
                return false;
            }
            ESP_LOGI(TAG, "GT911 found @ 0x%02X", (unsigned)addr);
        }

        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
        tp_io_cfg.dev_addr = addr;
        tp_io_cfg.scl_speed_hz = cfg.scl_speed_hz;
        tp_io_cfg.control_phase_bytes = 1;
        tp_io_cfg.dc_bit_offset = 0;
        tp_io_cfg.lcd_cmd_bits = 16;
        tp_io_cfg.flags.disable_control_phase = 1;
        esp_err_t err = esp_lcd_new_panel_io_i2c(cfg.bus, &tp_io_cfg, &tp_io);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Touch panel IO init failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = cfg.x_max;
        tp_cfg.y_max = cfg.y_max;
        tp_cfg.rst_gpio_num = cfg.rst;
        tp_cfg.int_gpio_num = cfg.intr;
        tp_cfg.process_coordinates = cfg.process_coordinates;

        err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &touch_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "GT911 init failed: %s", esp_err_to_name(err));
            return false;
        }

        if (cfg.intr != GPIO_NUM_NC && cfg.intr_any_edge)
        {
            // Which edge the GT911 pulses is not a property of the wiring, it is
            // burned into the module's config firmware (register 0x804D selects
            // rising / falling / low / high), so the pin map cannot tell us.
            // esp_lcd_touch commits to one edge from levels.interrupt, and the
            // wrong guess is silent: either nothing arrives, or presses only
            // register on release. Any-edge removes the guess. The cost is
            // waking twice per data frame instead of once, and a read that finds
            // no change is a single I2C transaction.
            //
            // The pull-up is for the idle line. If the module drives INT
            // open-drain, a bare input floats and storms the ISR; if it drives
            // push-pull, a 45k internal pull-up is far too weak to fight it.
            gpio_config_t int_cfg = {};
            int_cfg.pin_bit_mask = BIT64(cfg.intr);
            int_cfg.mode = GPIO_MODE_INPUT;
            int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
            int_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            int_cfg.intr_type = GPIO_INTR_ANYEDGE;
            if (gpio_config(&int_cfg) != ESP_OK)
                ESP_LOGW(TAG, "INT any-edge reconfigure failed; keeping single edge");
        }
        return true;
    }

    esp_lcd_touch_handle_t handle() const { return touch_; }
    bool ok() const { return touch_ != nullptr; }

private:
    esp_lcd_touch_handle_t touch_ = nullptr;
};
