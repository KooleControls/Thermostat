#pragma once

#include "BoardConfig.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// Shared I2C master bus for the Elecrow CrowPanel 2.1".
//
// The touch controller (CST826) and the PCF8574 IO expander sit on the same
// SDA/SCL pins, so the bus must be created exactly once and shared. This lazy
// singleton lets each driver (Display/Touch/Knob) keep its no-arg Init() and
// just ask for the bus, without a board-wide init-order contract.
// ──────────────────────────────────────────────────────────────

inline i2c_master_bus_handle_t BoardI2cBus()
{
    static i2c_master_bus_handle_t bus = nullptr;
    if (bus) return bus;

    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = (gpio_num_t)BoardConfig::I2C_PIN_SDA;
    cfg.scl_io_num = (gpio_num_t)BoardConfig::I2C_PIN_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK)
    {
        ESP_LOGE("I2cBus", "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        bus = nullptr;
    }
    return bus;
}
