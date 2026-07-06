#include "Board.h"
#include "esp_log.h"
#include "driver/uart.h"

Board::Board(ServiceProvider &ctx)
    : serviceProvider_(ctx)
{
}

void Board::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    // Shared I2C bus (GT911 touch + AHT20 sensor on SDA17/SCL18).
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = (gpio_num_t)BoardConfig::I2C_PIN_SDA;
    cfg.scl_io_num = (gpio_num_t)BoardConfig::I2C_PIN_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&cfg, &i2cBus_);
    if (err != ESP_OK)
    {
        // Not boot-critical: the board still runs (WiFi/web UI) without I2C.
        ESP_LOGE(TAG, "I2C bus create failed: %s", esp_err_to_name(err));
        i2cBus_ = nullptr;
    }

    if (i2cBus_ && ambientSensor_.Init(i2cBus_))
    {
        float celsius = 0;
        if (ambientSensor_.ReadTemperature(celsius))
            ESP_LOGI(TAG, "AHT20 ambient: %.1f degC", celsius);
        else
            ESP_LOGE(TAG, "AHT20 read failed (sensor absent or unresponsive)");
    }
    else
    {
        ESP_LOGE(TAG, "AHT20 init failed (sensor unavailable)");
    }

    // STM32L051 OpenTherm co-processor (owns the OT PHY). Init resets it
    // into its app (~900 ms warm-up) and Handshake says hello.
    if (otLink_.Init(UART_NUM_1, BoardConfig::OT_UART_TX, BoardConfig::OT_UART_RX,
                     BoardConfig::OT_STM32_BOOT0, BoardConfig::OT_STM32_NRST))
    {
        if (!otLink_.Handshake())
            ESP_LOGW(TAG, "STM32 OT co-processor not responding (manager will retry)");
    }
    else
    {
        ESP_LOGE(TAG, "OT link UART init failed");
    }

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}
