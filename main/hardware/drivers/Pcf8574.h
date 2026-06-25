#pragma once

#include "driver/i2c_master.h"
#include "esp_log.h"
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Reusable driver — PCF8574 8-bit I2C IO expander (and pin-compatible
// PCF8574A). The PCF8574 is a transparent quasi-bidirectional latch: a single
// byte sets all 8 pins, and a pin reads as an input only when its latch bit is
// high (write 1, then read). A shadow byte lets individual pins be driven
// without disturbing the others.
//
// Bus-agnostic and policy-free: the board supplies the i2c_master bus handle
// and the chip address, and layers any board-specific semantics (which pin is
// LCD reset, touch reset, a button, …) on top in its own wrapper.
// ──────────────────────────────────────────────────────────────

class Pcf8574
{
    static constexpr const char *TAG = "Pcf8574";

public:
    // initial_state is the shadow byte written on init (default: all pins high,
    // so inputs are released and outputs idle high).
    bool Init(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_speed_hz = 100000,
              uint8_t initial_state = 0xFF)
    {
        if (dev_) return true;
        if (!bus) return false;

        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = addr;
        dev_cfg.scl_speed_hz = scl_speed_hz;
        if (i2c_master_bus_add_device(bus, &dev_cfg, &dev_) != ESP_OK)
        {
            ESP_LOGW(TAG, "add_device failed");
            dev_ = nullptr;
            return false;
        }
        shadow_ = initial_state;
        return Write();
    }

    // Drive an output pin; keeps the shadow byte for the other pins.
    bool SetPin(int pin, bool high)
    {
        if (high) shadow_ |= (1u << pin);
        else      shadow_ &= ~(1u << pin);
        return Write();
    }

    // Read a pin as input (its latch bit must be high — true by default here).
    // Returns true (released/high) if the read fails, matching the idle state.
    bool GetPin(int pin)
    {
        uint8_t in = 0;
        if (i2c_master_receive(dev_, &in, 1, 100) != ESP_OK) return true;
        return (in >> pin) & 0x1;
    }

    bool ok() const { return dev_ != nullptr; }

private:
    bool Write()
    {
        if (!dev_) return false;
        return i2c_master_transmit(dev_, &shadow_, 1, 100) == ESP_OK;
    }

    i2c_master_dev_handle_t dev_ = nullptr;
    uint8_t shadow_ = 0xFF;
};
