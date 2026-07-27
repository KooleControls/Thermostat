#include "DisplayManager.h"
#include "SettingsManager/SettingsManager.h"
#include "Board.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

DisplayManager::DisplayManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
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

    // Registered even when headless: the PIN is also editable over the web
    // settings UI, and that must not depend on a panel being present.
    pinGate_.Register(serviceProvider_.getSettingsManager());

    if (serviceProvider_.getBoard().GetPanel() == nullptr)
    {
        ESP_LOGW(TAG, "No panel (headless) — UI disabled");
        return;
    }
    if (!InitLvgl()) return;

    wifiScreen_.Init();   // hands the scan worker its NetworkManager

    // Touch (best-effort — display still works without it).
    if (serviceProvider_.getBoard().GetTouch())
    {
        lvgl_port_touch_cfg_t tcfg = {};
        tcfg.disp = lvDisplay_;
        tcfg.handle = serviceProvider_.getBoard().GetTouch();
        if (lvgl_port_add_touch(&tcfg) == nullptr)
            ESP_LOGW(TAG, "lvgl_port_add_touch failed — touch disabled");
    }

    Go(ScreenId::Home);

    if (lvgl_port_lock(0))
    {
        lv_timer_create(IdleTimerCb, kIdleTickMs, this);
        lvgl_port_unlock();
    }

    serviceProvider_.getBoard().SetBacklight(true);

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

bool DisplayManager::InitLvgl()
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 8192;
    port_cfg.task_affinity = 1;   // pin to core 1, leave core 0 for WiFi
    if (lvgl_port_init(&port_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "lvgl_port_init failed");
        return false;
    }

    // Partial draw buffer (20 lines) in internal DMA RAM. Rendering into a
    // PSRAM buffer competes with the panel DMA for PSRAM bandwidth → on-screen
    // artifacts; internal RAM + bounce-buffer mode is the proven config.
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.panel_handle = serviceProvider_.getBoard().GetPanel();
    disp_cfg.buffer_size = 480 * 20;
    disp_cfg.double_buffer = false;
    disp_cfg.hres = 480;
    disp_cfg.vres = 480;
    disp_cfg.monochrome = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.buff_spiram = false;
    disp_cfg.flags.swap_bytes = false;
    disp_cfg.flags.full_refresh = false;

    lvgl_port_display_rgb_cfg_t rgb_cfg = {};
    rgb_cfg.flags.bb_mode = true;         // bounce-buffer mode (anti-glitch)
    rgb_cfg.flags.avoid_tearing = false;

    lvDisplay_ = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (lvDisplay_ == nullptr)
    {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return false;
    }
    return true;
}

Screen* DisplayManager::Resolve(ScreenId id)
{
    switch (id)
    {
        case ScreenId::Home:     return &homeScreen_;
        case ScreenId::Pin:      return &pinScreen_;
        case ScreenId::Settings: return &settingsScreen_;
        case ScreenId::Wifi:     return &wifiScreen_;
        case ScreenId::Info:     return &infoScreen_;
    }
    return &homeScreen_;
}

void DisplayManager::Go(ScreenId id)
{
    if (lvDisplay_ == nullptr) return;   // headless

    if (id == ScreenId::Pin && !pinGate_.Required())
        id = ScreenId::Settings;

    if (!lvgl_port_lock(0))
    {
        ESP_LOGW(TAG, "Could not take the LVGL lock — navigation skipped");
        return;
    }
    Resolve(id)->Load();
    current_ = id;
    lvgl_port_unlock();
}

void DisplayManager::IdleTimerCb(lv_timer_t *t)
{
    auto *self = static_cast<DisplayManager *>(lv_timer_get_user_data(t));
    if (self->current_ == ScreenId::Home) return;
    if (lv_display_get_inactive_time(self->lvDisplay_) < kIdleTimeoutMs) return;

    // Never leave a unit sitting in the service menu, unlocked, in someone's
    // hallway. Already inside the LVGL task, so load directly.
    ESP_LOGI(TAG, "Idle — returning to home screen");
    self->homeScreen_.Load();
    self->current_ = ScreenId::Home;
}
