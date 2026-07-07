#include "Board.h"
#include "DisplayInit.h"
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

    // Backlight GPIO — held off until DisplayManager has a first frame up.
    gpio_config_t bkCfg = {};
    bkCfg.pin_bit_mask = 1ULL << BoardConfig::LCD_PIN_BACKLIGHT;
    bkCfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bkCfg);
    gpio_set_level((gpio_num_t)BoardConfig::LCD_PIN_BACKLIGHT, 0);

    // ST7701 panel. MUST init before the OT link below: the 3-wire SPI init
    // uses GPIO11/12, which otLink_.Init() then reclaims (gpio_reset_pin) as
    // its UART. auto_del_panel_io stays false (true blanks this panel).
    {
        St7701Config cfg{};
        cfg.spi_cs  = (gpio_num_t)BoardConfig::LCD_SPI_CS;
        cfg.spi_sck = (gpio_num_t)BoardConfig::LCD_SPI_SCK;
        cfg.spi_sda = (gpio_num_t)BoardConfig::LCD_SPI_SDA;
        cfg.spi_mode = 0;
        cfg.de = (gpio_num_t)BoardConfig::LCD_PIN_DE;
        cfg.vsync = (gpio_num_t)BoardConfig::LCD_PIN_VSYNC;
        cfg.hsync = (gpio_num_t)BoardConfig::LCD_PIN_HSYNC;
        cfg.pclk = (gpio_num_t)BoardConfig::LCD_PIN_PCLK;
        cfg.data_pins = BoardConfig::LCD_DATA_PINS;
        cfg.h_res = BoardConfig::LCD_H_RES;
        cfg.v_res = BoardConfig::LCD_V_RES;
        cfg.pclk_hz = BoardConfig::LCD_PIXEL_CLOCK_HZ;
        cfg.hsync_pulse_width = BoardConfig::LCD_HSYNC_PULSE_WIDTH;
        cfg.hsync_back_porch = BoardConfig::LCD_HSYNC_BACK_PORCH;
        cfg.hsync_front_porch = BoardConfig::LCD_HSYNC_FRONT_PORCH;
        cfg.vsync_pulse_width = BoardConfig::LCD_VSYNC_PULSE_WIDTH;
        cfg.vsync_back_porch = BoardConfig::LCD_VSYNC_BACK_PORCH;
        cfg.vsync_front_porch = BoardConfig::LCD_VSYNC_FRONT_PORCH;
        cfg.pclk_active_neg = BoardConfig::LCD_PCLK_ACTIVE_NEG;
        cfg.pclk_idle_high = BoardConfig::LCD_PCLK_IDLE_HIGH;
        cfg.clk_src = LCD_CLK_SRC_PLL160M;   // DIYLESS pins PLL160M (anti-jitter)
        cfg.reset_gpio = (gpio_num_t)BoardConfig::LCD_PIN_RESET;
        cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        cfg.init_cmds = DIYLESS_ST7701_INIT;
        cfg.init_cmds_size = sizeof(DIYLESS_ST7701_INIT) / sizeof(DIYLESS_ST7701_INIT[0]);
        cfg.mirror_by_cmd = true;
        cfg.auto_del_panel_io = false;
        if (!panel_.Init(cfg))
            ESP_LOGE(TAG, "ST7701 panel init failed (continuing headless)");
    }

    // GT911 touch on the shared I2C bus (auto-probe 0x5D/0x14).
    if (i2cBus_)
    {
        Gt911Config tcfg{};
        tcfg.bus = i2cBus_;
        tcfg.x_max = BoardConfig::LCD_H_RES;
        tcfg.y_max = BoardConfig::LCD_V_RES;
        if (!touch_.Init(tcfg))
            ESP_LOGW(TAG, "GT911 touch init failed (continuing without touch)");
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
