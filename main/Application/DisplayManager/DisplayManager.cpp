#include "DisplayManager.h"
#include "Screens.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

DisplayManager::DisplayManager(ServiceProvider &ctx)
    : serviceProvider_(ctx)
{
}

void DisplayManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    if (!display_.Init())
    {
        ESP_LOGE(TAG, "Display hardware init failed");
        return;
    }
    if (!InitLvgl()) return;
    InitTouch();  // best-effort; display still works without touch

    BuildUi();
    display_.Backlight(true);  // first frame is up — light the panel

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (%dx%d)", Display::Width(), Display::Height());
}

bool DisplayManager::InitLvgl()
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 8192;
    port_cfg.task_affinity = 1;  // pin LVGL to core 1, leave core 0 for WiFi
    if (lvgl_port_init(&port_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "lvgl_port_init failed");
        return false;
    }

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.panel_handle = display_.panel();
    // Partial draw buffer (20 lines) in internal DMA RAM: rendering into a
    // PSRAM buffer made every redraw compete with the panel DMA for PSRAM
    // bandwidth, which showed up as on-screen artifacts during updates.
    disp_cfg.buffer_size = Display::Width() * 20;
    disp_cfg.double_buffer = false;
    disp_cfg.hres = Display::Width();
    disp_cfg.vres = Display::Height();
    disp_cfg.monochrome = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.buff_spiram = false;
    disp_cfg.flags.swap_bytes = false;
    disp_cfg.flags.full_refresh = false;

    lvgl_port_display_rgb_cfg_t rgb_cfg = {};
    rgb_cfg.flags.bb_mode = true;         // bounce-buffer mode (anti-glitch)
    rgb_cfg.flags.avoid_tearing = false;  // bb_mode handles sync; needs only 1 fb

    lvDisplay_ = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (lvDisplay_ == nullptr)
    {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return false;
    }
    return true;
}

void DisplayManager::InitTouch()
{
    if (!touch_.Init())
    {
        ESP_LOGW(TAG, "Touch unavailable — continuing without it");
        return;
    }

    lvgl_port_touch_cfg_t touch_cfg = {};
    touch_cfg.disp = lvDisplay_;
    touch_cfg.handle = touch_.handle();
    if (lvgl_port_add_touch(&touch_cfg) == nullptr)
        ESP_LOGW(TAG, "lvgl_port_add_touch failed — touch disabled");
    else
        ESP_LOGI(TAG, "GT911 touch ready");
}

void DisplayManager::BuildUi()
{
    if (!lvgl_port_lock(0))
    {
        ESP_LOGW(TAG, "Could not lock LVGL to build UI");
        return;
    }

    ShowThermostatScreen(serviceProvider_);

    lvgl_port_unlock();
}
