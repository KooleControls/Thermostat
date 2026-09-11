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
    serviceProvider_.getSettingsManager().Register(
        { &dimPercent_, &fullPercent_, &dimAfterS_,
          &SettingsMenuScreen::lightTheme_ });

    // Before anything is built: every screen copies the palette into per-widget
    // local styles as it goes, so the mode has to be settled first. Registered
    // and applied even when headless — the setting is editable over the web UI
    // whether or not this unit has a panel to show it on.
    UiTheme::SetMode(SettingsMenuScreen::ThemeMode());

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
        lv_indev_t* indev = lvgl_port_add_touch(&tcfg);
        if (indev == nullptr)
            ESP_LOGW(TAG, "lvgl_port_add_touch failed — touch disabled");
        else
            ArmTouchBackstop(indev);
    }

    Go(ScreenId::Home);

    if (lvgl_port_lock(0))
    {
        lv_timer_create(TickCb, kTickMs, this);
        lvgl_port_unlock();
    }

    // Full brightness once there is something to look at; the tick dims it
    // once the panel has been left alone.
    serviceProvider_.getBoard().SetBacklightPercent(
        static_cast<uint8_t>(fullPercent_.Get() > 100 ? 100 : fullPercent_.Get()));
    backlightFull_ = true;

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

bool DisplayManager::InitLvgl()
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 8192;
    port_cfg.task_affinity = 1;   // pin to core 1, leave core 0 for WiFi

    // Above the managers, deliberately. The port defaults to 4 and every task
    // this application starts — opentherm, climate, roomtemp, ble_dispatch,
    // ConsoleBroadcast, wifi_scan — runs at 5, so by default a fingertip waited
    // behind whichever of them happened to be awake. The work they do between
    // sleeps is short, but a press that lands during one inherits its whole
    // remaining slice, and that is the jitter you feel rather than the average.
    //
    // 6 is above the managers and far below the stacks that must not be starved
    // (WiFi 23, esp_timer 22, NimBLE host ~21, lwIP 18). The exposure runs the
    // other way now: a full-screen redraw is ~47 ms of render, and the managers
    // wait that out. They are all multi-second loops or blocked on a UART reply,
    // so none of them can miss a deadline it actually has.
    port_cfg.task_priority = 6;
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

// Because the board hands esp_lvgl_port a real INT pin, the port puts the input
// device in LV_INDEV_MODE_EVENT: the controller is read when the interrupt says
// there is something to read, rather than once per LV_DEF_REFR_PERIOD — which
// was the Kconfig default of 33 ms when this landed, and is the largest single
// term this removed from the touch-to-visible latency we measured.
//
// Event mode pauses LVGL's read timer, and that is the part worth guarding.
// LVGL's idea of "pressed" only changes when a read happens, so one lost
// release edge — a missed interrupt, an ISR that lands while the pin is being
// reconfigured — leaves a button held down until someone touches the glass
// again. Resuming the timer at a slow period puts a floor under that: a lost
// release clears within kTouchBackstopMs instead of never. It is still a third
// of the polling this board did before, and it costs one I2C transaction.
void DisplayManager::ArmTouchBackstop(lv_indev_t* indev)
{
    if (!lvgl_port_lock(0)) return;
    if (lv_timer_t* readTimer = lv_indev_get_read_timer(indev))
    {
        lv_timer_set_period(readTimer, kTouchBackstopMs);
        lv_timer_resume(readTimer);
    }
    lvgl_port_unlock();
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

void DisplayManager::Restyle()
{
    if (lvDisplay_ == nullptr) return;   // headless — the setting still persists

    if (!lvgl_port_lock(0))
    {
        ESP_LOGW(TAG, "Could not take the LVGL lock — restyle skipped");
        return;
    }

    UiTheme::SetMode(SettingsMenuScreen::ThemeMode());

    // The screen on the panel is swapped, not dropped and rebuilt in place:
    // lv_obj_delete() on the active screen nulls the display's own pointer to
    // it (and says so, loudly, in the log), so the replacement is loaded first
    // and the outgoing tree freed once it is no longer the one being shown.
    Screen* current = Resolve(current_);
    lv_obj_t* outgoing = current->Detach();

    // Every other screen too, not just the visible one: they are already built
    // and would otherwise come back in the old palette when navigated to.
    for (ScreenId id : kAllScreens)
    {
        Screen* screen = Resolve(id);
        if (screen != current) screen->Rebuild();
    }

    current->Load();
    if (outgoing) lv_obj_delete(outgoing);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "Restyled — %s theme", UiTheme::IsLight() ? "light" : "dark");
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

RequestError DisplayManager::Cmd_UiTheme(CommandContext& ctx)
{
    const bool was = SettingsMenuScreen::lightTheme_.Get();
    bool want = was;   // absent "light" leaves this alone — a plain read
    RETURN_IF_ERROR(ctx.readArgs(Optional("light", want)));

    if (want != was)
    {
        SettingsMenuScreen::lightTheme_.Set(want);
        serviceProvider_.getSettingsManager().Save();
        Restyle();   // takes the LVGL lock itself; this runs on the command task
    }

    JsonObject resp(ctx.out);
    resp.field("ok", true);
    resp.field("light", SettingsMenuScreen::lightTheme_.Get());
    resp.field("headless", lvDisplay_ == nullptr);
    return RequestError::Ok;
}

bool DisplayManager::ParseScreen(const char *name, ScreenId &out)
{
    for (ScreenId id : kAllScreens)
    {
        if (strcmp(name, ScreenName(id)) == 0)
        {
            out = id;
            return true;
        }
    }
    return false;
}

RequestError DisplayManager::Cmd_UiGo(CommandContext& ctx)
{
    char name[16] = {};
    RETURN_IF_ERROR(ctx.readArgs(Optional("screen", name)));

    ScreenId target = current_;
    bool     haveName = name[0] != '\0';
    bool     known = haveName && ParseScreen(name, target);

    if (haveName && known)
        Go(target);   // takes the LVGL lock itself; blocks until it has it

    JsonObject resp(ctx.out);
    resp.field("ok", !haveName || known);
    resp.field("screen", ScreenName(current_));
    resp.field("headless", lvDisplay_ == nullptr);
    if (haveName && !known)
        resp.field("error", "unknown screen");
    return RequestError::Ok;
}

// Dim when the panel is left alone, full the moment it is touched.
//
// "Touched" is LVGL's own inactivity counter, which every input device resets,
// so this needs no hook into the touch driver and cannot disagree with it about
// what counts as use. The waking touch is still delivered to the widget under
// it: on a thermostat the finger is usually already on -/+, and swallowing that
// press to "just wake the screen" would cost a second press every time.
void DisplayManager::ServiceBacklight(uint32_t idleMs)
{
    // Capped before the ×1000: an out-of-range setting overflowing to a tiny
    // timeout would dim the panel instantly and read as a broken backlight.
    uint32_t afterS = dimAfterS_.Get();
    if (afterS > 3600) afterS = 3600;

    bool full = idleMs < afterS * 1000;
    if (full == backlightFull_) return;

    uint32_t pct = full ? fullPercent_.Get() : dimPercent_.Get();
    if (pct > 100) pct = 100;
    serviceProvider_.getBoard().SetBacklightPercent(static_cast<uint8_t>(pct));
    backlightFull_ = full;
}

void DisplayManager::TickCb(lv_timer_t *t)
{
    auto *self = static_cast<DisplayManager *>(lv_timer_get_user_data(t));
    uint32_t idleMs = lv_display_get_inactive_time(self->lvDisplay_);

    self->ServiceBacklight(idleMs);

    if (self->current_ == ScreenId::Home) return;
    if (idleMs < kIdleTimeoutMs) return;

    // Never leave a unit sitting in the service menu, unlocked, in someone's
    // hallway. Already inside the LVGL task, so load directly.
    ESP_LOGI(TAG, "Idle — returning to home screen");
    self->homeScreen_.Load();
    self->current_ = ScreenId::Home;
}
