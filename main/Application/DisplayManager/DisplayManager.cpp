#include "DisplayManager.h"
#include "SettingsManager/SettingsManager.h"
#include "CommandManager/CommandManager.h"
#include "Board.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <cstring>

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

    // Likewise for uiGo — headless it reports the shell's idea of the current
    // screen and navigates nothing, which is a truthful answer rather than a
    // missing command.
    serviceProvider_.getCommandManager().Register(this, commands_);

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

    // Full brightness once there is something to look at. Brightness is a lever
    // in the self-heating test rig too (ThermalTestManager, which initializes
    // after this and may darken the panel again on purpose).
    serviceProvider_.getBoard().SetBacklightPercent(100);

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

    // Direct mode into the panel's two PSRAM framebuffers.
    //
    // Replaces a 10-line internal staging buffer + bounce-buffer DMA. Why, from
    // measurements on this board (LVGL sysmon, screen changes driven by uiGo):
    //
    //   * A full-screen redraw cost ~108 ms of render with a 10-line buffer and
    //     ~54 ms with a 20-line one. Halving the pass count halved the time, so
    //     per-pass overhead dominated, not pixel work — each pass re-walks the
    //     object tree and re-clips every object. 480x480 in 10-line slices is
    //     48 passes. Direct mode is ONE.
    //   * The staging buffer then had to be copied into the framebuffer. In
    //     direct mode LVGL renders into the framebuffer, so that copy is gone.
    //   * No bounce buffers means no GDMA EOF interrupt every 480 us with a CPU
    //     memcpy out of PSRAM, so the deadline that produced the vertical slip
    //     no longer exists to be missed.
    //   * Frees ~38 KB of internal DRAM (19 KB staging + 19 KB bounce buffers),
    //     which is what the BLE stack was starved of.
    //
    // The old comment here said rendering into PSRAM competes with the panel DMA
    // and produced artifacts. That was measured without double buffering and
    // without a VSYNC-synced swap — avoid_tearing supplies both, which is
    // precisely what stops the renderer and the scanout touching one buffer.
    //
    // With avoid_tearing the port ignores buffer_size and adopts the panel's own
    // framebuffers, but direct mode still asserts buffer_size == hres*vres.
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.panel_handle = serviceProvider_.getBoard().GetPanel();
    disp_cfg.buffer_size = 480 * 480;
    disp_cfg.double_buffer = true;
    disp_cfg.hres = 480;
    disp_cfg.vres = 480;
    disp_cfg.monochrome = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = false;
    disp_cfg.flags.buff_spiram = false;
    disp_cfg.flags.swap_bytes = false;
    disp_cfg.flags.full_refresh = false;
    disp_cfg.flags.direct_mode = true;

    lvgl_port_display_rgb_cfg_t rgb_cfg = {};
    rgb_cfg.flags.bb_mode = false;
    rgb_cfg.flags.avoid_tearing = true;

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
        case ScreenId::Ble:      return &bleScreen_;
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

const char* DisplayManager::ScreenName(ScreenId id)
{
    switch (id)
    {
        case ScreenId::Home:     return "home";
        case ScreenId::Pin:      return "pin";
        case ScreenId::Settings: return "settings";
        case ScreenId::Wifi:     return "wifi";
        case ScreenId::Ble:      return "ble";
        case ScreenId::Info:     return "info";
    }
    return "home";
}

bool DisplayManager::ParseScreen(const char *name, ScreenId &out)
{
    static constexpr ScreenId kAll[] = {
        ScreenId::Home, ScreenId::Pin, ScreenId::Settings,
        ScreenId::Wifi, ScreenId::Ble, ScreenId::Info,
    };
    for (ScreenId id : kAll)
    {
        if (strcmp(name, ScreenName(id)) == 0)
        {
            out = id;
            return true;
        }
    }
    return false;
}

void DisplayManager::Cmd_UiGo(Stream &in, Stream &out)
{
    JsonReader<128> json(in);

    char    name[16] = {};
    ScreenId target = current_;
    bool    haveName = json.GetString("screen", name, sizeof(name));
    bool    known = haveName && ParseScreen(name, target);

    if (haveName && known)
        Go(target);   // takes the LVGL lock itself; blocks until it has it

    JsonObject resp(out);
    resp.field("ok", !haveName || known);
    resp.field("screen", ScreenName(current_));
    resp.field("headless", lvDisplay_ == nullptr);
    if (haveName && !known)
        resp.field("error", "unknown screen");
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
