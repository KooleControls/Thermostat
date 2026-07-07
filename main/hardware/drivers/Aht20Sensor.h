#pragma once

#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include <cstdint>
#include "interfaces/TemperatureSensor.h"
#include "interfaces/HumiditySensor.h"

// ──────────────────────────────────────────────────────────────
// Reusable driver — AHT20 temperature/humidity sensor over I2C (addr 0x38).
//
// Bus-agnostic: Init(bus) takes a shared i2c_master bus handle, so the same
// driver works on any board (the board supplies its own bus). Implements the
// TemperatureSensor and HumiditySensor role interfaces; the board's Board
// class owns an instance and passes in its shared I2C bus
// (see diyless_thermostat_3/Board.h).
//
// A measurement is trigger-then-read (~80 ms conversion). To avoid blocking
// the caller's poll task, reads are non-blocking: a poll latches the previous
// (now-complete) measurement and re-triggers the next. The first sample is
// taken (briefly blocking) in Init() so the UI has a real reading at startup.
// Failure handling: a failed Trigger() or Latch() is retried on the next
// Service() call; after MAX_CONSECUTIVE_FAILURES in a row, have_ is cleared
// so ReadTemperature()/ReadHumidity() report "no data" instead of serving a
// frozen stale value forever.
// ──────────────────────────────────────────────────────────────

class Aht20Sensor : public TemperatureSensor, public HumiditySensor
{
    static constexpr const char *TAG = "AHT20";
    static constexpr uint8_t ADDR = 0x38;
    static constexpr int64_t MEASURE_US = 80000;  // 80 ms conversion time

public:
    bool Init(i2c_master_bus_handle_t bus)
    {
        if (!bus) return false;

        i2c_device_config_t devcfg = {};
        devcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        devcfg.device_address = ADDR;
        devcfg.scl_speed_hz = 400000;
        if (i2c_master_bus_add_device(bus, &devcfg, &dev_) != ESP_OK || !dev_)
        {
            ESP_LOGW(TAG, "AHT20 not found on I2C bus");
            dev_ = nullptr;
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(40));  // power-on settle

        // Calibrate if the status' calibration bit (bit 3) is clear.
        uint8_t status = 0;
        if (i2c_master_receive(dev_, &status, 1, 100) == ESP_OK && !(status & 0x08))
        {
            const uint8_t init_cmd[] = {0xBE, 0x08, 0x00};
            i2c_master_transmit(dev_, init_cmd, sizeof(init_cmd), 100);
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // First sample (briefly blocking) so the UI starts with a real reading.
        Trigger();
        vTaskDelay(pdMS_TO_TICKS(85));
        Latch();
        Trigger();  // start the next; subsequent reads are non-blocking
        ESP_LOGI(TAG, "AHT20 ready");
        return true;
    }

    bool ReadTemperature(float &celsius) override
    {
        Service();
        if (!have_) return false;
        celsius = temp_;
        return true;
    }

    bool ReadHumidity(float &percent) override
    {
        Service();
        if (!have_) return false;
        percent = humidity_;
        return true;
    }


private:
    static constexpr int MAX_CONSECUTIVE_FAILURES = 3;

    void Trigger()
    {
        const uint8_t cmd[] = {0xAC, 0x33, 0x00};
        if (i2c_master_transmit(dev_, cmd, sizeof(cmd), 100) == ESP_OK)
        {
            pending_ = true;
            triggerUs_ = esp_timer_get_time();
        }
        else
        {
            Fail();
        }
    }

    // If a triggered measurement has had time to complete, latch it and start
    // the next one. Non-blocking — safe to call from the poll task. Also
    // recovers from a previously failed Trigger() (pending_ never got set).
    void Service()
    {
        if (!dev_) return;

        if (!pending_)
        {
            Trigger();
            return;
        }

        if (esp_timer_get_time() - triggerUs_ >= MEASURE_US)
        {
            Latch();
            Trigger();
        }
    }

    void Latch()
    {
        if (!pending_) return;
        pending_ = false;
        uint8_t b[7] = {};
        if (i2c_master_receive(dev_, b, sizeof(b), 100) != ESP_OK)
        {
            Fail();
            return;
        }
        if (b[0] & 0x80)  // bit 7 = still busy — drop this sample
        {
            Fail();
            return;
        }

        uint32_t rh = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
        uint32_t t  = (((uint32_t)b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];
        humidity_ = rh * 100.0f / 1048576.0f;        // 2^20
        temp_     = t * 200.0f / 1048576.0f - 50.0f;
        have_ = true;
        consecutiveFailures_ = 0;
    }

    // Track consecutive Trigger()/Latch() failures; after enough in a row,
    // stop serving the last (now possibly stale-forever) reading.
    void Fail()
    {
        if (++consecutiveFailures_ >= MAX_CONSECUTIVE_FAILURES)
            have_ = false;
    }

    i2c_master_dev_handle_t dev_ = nullptr;
    bool pending_ = false;
    bool have_ = false;
    int64_t triggerUs_ = 0;
    float temp_ = 0.0f;
    float humidity_ = 0.0f;
    int consecutiveFailures_ = 0;
};
