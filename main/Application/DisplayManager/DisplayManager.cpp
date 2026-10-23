#include "DisplayManager.h"
#include "SettingsManager/SettingsManager.h"
#include "CommandManager/CommandManager.h"
#include "Board.h"
#include "BootLogo.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "esp_app_desc.h"
#include "esp_cache.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

namespace {

constexpr uint16_t Rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 19) & 0x1F) << 11 | ((rgb >> 10) & 0x3F) << 5 | ((rgb >> 3) & 0x1F));
}

// `a` of `fg` over `bg`, a in 0..255.
uint16_t Blend565(uint16_t fg, uint16_t bg, uint32_t a)
{
    auto mix = [a](uint32_t f, uint32_t b) { return (f * a + b * (255 - a) + 127) / 255; };
    return (uint16_t)(mix(fg >> 11, bg >> 11) << 11 |
                      mix((fg >> 5) & 0x3F, (bg >> 5) & 0x3F) << 5 |
                      mix(fg & 0x1F, bg & 0x1F));
}

// One line of text, centred, for the boot logo — before LVGL exists, so the
// glyphs are read straight out of one of its built-in fonts. The lookup is
// LVGL's own (pure: no heap, no LVGL state); the bitmap is decoded here, which
// covers the plain 4 bpp layout the Montserrat fonts use and nothing else.
void DrawBootText(uint16_t* fb, int fbW, int fbH, int top, const char* text,
                  const lv_font_t* font, uint16_t colour)
{
    auto* fdsc = static_cast<const lv_font_fmt_txt_dsc_t*>(font->dsc);
    if (fdsc->bitmap_format != LV_FONT_FMT_TXT_PLAIN || fdsc->bpp != 4 || fdsc->stride != 0)
        return;

    int width = 0;
    for (const char* c = text; *c; ++c)
    {
        lv_font_glyph_dsc_t g = {};
        if (font->get_glyph_dsc(font, &g, (uint8_t)c[0], (uint8_t)c[1])) width += g.adv_w;
    }

    int penX = (fbW - width) / 2;
    const int baseline = top + font->line_height - font->base_line;
    for (const char* c = text; *c; ++c)
    {
        lv_font_glyph_dsc_t g = {};
        if (!font->get_glyph_dsc(font, &g, (uint8_t)c[0], (uint8_t)c[1])) continue;

        // Nibbles run on across rows: the font has no row stride.
        const uint8_t* bits = fdsc->glyph_bitmap + fdsc->glyph_dsc[g.gid.index].bitmap_index;
        const int gx = penX + g.ofs_x;
        const int gy = baseline - g.box_h - g.ofs_y;
        for (int i = 0; i < g.box_w * g.box_h; ++i)
        {
            const uint32_t nibble = (i & 1) ? (bits[i >> 1] & 0x0F) : (bits[i >> 1] >> 4);
            const int x = gx + i % g.box_w;
            const int y = gy + i / g.box_w;
            if (nibble == 0 || x < 0 || x >= fbW || y < 0 || y >= fbH) continue;
            uint16_t& px = fb[y * fbW + x];
            px = Blend565(colour, px, nibble * 17);
        }
        penX += g.adv_w;
    }
}

}   // namespace

DisplayManager::DisplayManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

// Draws into both framebuffers, since which one scans out first is the
// driver's business, and LVGL later adopts the same two buffers: its first
// frame is a full redraw that swaps in over the logo, so there is no gap.
void DisplayManager::ShowBootLogo()
{
    Board& board = serviceProvider_.getBoard();
    esp_lcd_panel_handle_t panel = board.GetPanel();
    if (panel == nullptr) return;   // headless, or the panel failed to come up

    void* fbs[2] = {};
    size_t count = 2;
    if (esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fbs[0], &fbs[1]) != ESP_OK)
    {
        count = 1;
        if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fbs[0]) != ESP_OK)
        {
            ESP_LOGW(TAG, "No framebuffer to draw the boot logo into");
            return;
        }
    }

    constexpr int W = BoardConfig::LCD_H_RES;
    constexpr int H = BoardConfig::LCD_V_RES;
    const BootLogo& logo = kBootLogo;
    const int x0 = (W - logo.w) / 2;
    const int y0 = (H - logo.h) / 2;

    // The firmware version under the logo, as the release tag spells it, so a
    // unit says what it runs before anything else is up. Grey of the light
    // theme's dim text: the theme itself is not set yet.
    char version[40];
    snprintf(version, sizeof(version), "V%s", esp_app_get_description()->version);
    constexpr int kVersionGap = 24;   // logo bottom to text top
    constexpr uint16_t kVersionColour = Rgb565(0x8A8A8E);

    for (size_t i = 0; i < count; ++i)
    {
        auto* fb = static_cast<uint16_t*>(fbs[i]);
        for (int p = 0; p < W * H; ++p) fb[p] = logo.bg;

        int x = 0, y = 0;   // position inside the logo
        for (size_t r = 0; r < logo.runs; ++r)
        {
            uint16_t n = logo.rle[2 * r];
            const uint16_t colour = logo.rle[2 * r + 1];
            while (n--)
            {
                fb[(y0 + y) * W + x0 + x] = colour;
                if (++x == logo.w) { x = 0; ++y; }
            }
        }

        DrawBootText(fb, W, H, y0 + logo.h + kVersionGap, version,
                     &lv_font_montserrat_20, kVersionColour);

        // The scanout reads these through the bounce buffers' CPU copy today,
        // which sees the cache; write back anyway, so a layout that DMAs
        // straight from PSRAM shows the logo too.
        esp_cache_msync(fb, W * H * sizeof(uint16_t),
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }

    // Full brightness, as DisplayManager::Init leaves it by default; the
    // brightness setting is not registered yet this early.
    board.SetBacklightPercent(100);
    ESP_LOGI(TAG, "Boot logo up");
}

void DisplayManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getSettingsManager().Register(
        { &dimPercent_, &fullPercent_, &dimAfterS_,
          &SettingsMenuScreen::lightTheme_ });

    // Before anything is built: every screen copies the palette into per-widget
    // local styles as it goes, so the mode has to be settled first. Registered
    // and applied even when headless — the setting is editable over the web UI
    // whether or not this unit has a panel to show it on.
    UiTheme::SetMode(SettingsMenuScreen::ThemeMode());

    // Also before anything is built: the screens size their round shapes from
    // it. The panel's pixels are not square (see UiTheme::RoundHeight).
    UiTheme::SetPixelPitch(serviceProvider_.getBoard().GetPixelPitchXNm(),
                           serviceProvider_.getBoard().GetPixelPitchYNm());

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
    //   * Frees the 19 KB staging buffer of internal DRAM, which is what the BLE
    //     stack was starved of.
    //
    // The bounce buffers went with it at first, and are back under the direct
    // mode: 10 lines, 19 KB of internal RAM. Streaming straight out of PSRAM
    // left the DMA a few microseconds of FIFO, and PSRAM shares its SPI bus
    // with the flash, so a burst of flash traffic — NimBLE storing a bond at
    // the end of every pairing — starved it and shifted the picture for good.
    // Fed from internal RAM, the DMA has ~500 us of slack instead. The refill
    // is a CPU memcpy out of PSRAM in an ISR, the deadline once blamed for a
    // vertical slip. That slip predates SPIRAM_XIP_FROM_PSRAM, when every flash
    // write turned the cache off and the refill could not run at all — the
    // likely cause, not a confirmed one. If it slips anyway, the driver sees
    // the missed refill and restarts at the next VSYNC: one bad frame, not a
    // permanent shift. bb_mode tells the port to swap on the bounce frame's
    // end rather than on VSYNC.
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
    rgb_cfg.flags.bb_mode = true;
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
        case ScreenId::BleTest:  return &bleTestScreen_;
    }
    return &homeScreen_;
}

void DisplayManager::Go(ScreenId id)
{
    if (lvDisplay_ == nullptr) return;   // headless

    if (!lvgl_port_lock(0))
    {
        ESP_LOGW(TAG, "Could not take the LVGL lock — navigation skipped");
        return;
    }
    Resolve(id)->Load();
    current_ = id;
    ResyncPanel();
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
    ResyncPanel();   // the theme setting was just written to flash

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
        case ScreenId::BleTest:  return "bletest";
    }
    return "home";
}

RequestError DisplayManager::Cmd_UiResync(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    // Through the same queue as the automatic ones, so it levels the frame
    // buffers first; this runs on the command task, hence the lock.
    if (lvDisplay_ != nullptr && lvgl_port_lock(0))
    {
        ResyncPanel();
        lvgl_port_unlock();
    }

    JsonObject resp(ctx.out);
    resp.field("ok", true);
    resp.field("headless", lvDisplay_ == nullptr);
    return RequestError::Ok;
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

// A sideways shift of the picture is permanent (see Board::ResyncPanel) and
// cannot be detected, so it is undone blind: after every screen change, which
// makes a shift last until the next tap through the menus at most, and when a
// screen asks after a flash write of its own. Deferred, not done on the spot,
// so the change that asked for it has finished drawing first. Must be called
// with the LVGL lock held.
void DisplayManager::ResyncPanel()
{
    resyncPending_ = true;
    resyncRequestedMs_ = lv_tick_get();
}

// The restart shows frame buffer 0 for one frame, whichever buffer is current.
// In direct mode LVGL brings the other buffer up to date only at the start of
// its next refresh, so straight after a screen change one of the two still
// holds the previous screen — and restarting then flashed that screen up for a
// frame, half the time. So both are levelled first: a one-pixel invalidation
// makes the next refresh copy every pending area across, and lv_refr_now runs
// it here, flip included. After that both buffers hold the same picture and
// the restart has nothing to show but the screen already on the panel.
void DisplayManager::ServicePanelResync()
{
    if (!resyncPending_ || lv_tick_elaps(resyncRequestedMs_) < 300) return;
    resyncPending_ = false;

    lv_area_t pixel = { 0, 0, 0, 0 };
    lv_obj_invalidate_area(lv_screen_active(), &pixel);
    lv_refr_now(lvDisplay_);

    serviceProvider_.getBoard().ResyncPanel();
}

// The BOOT button on the back raises the reset dialog over whatever is up. It
// has to be held for kBootHoldTicks first, and let go before it can raise the
// dialog again.
void DisplayManager::ServiceBootButton(uint32_t idleMs)
{
    // Left unanswered, the dialog goes the way the service menu does.
    if (resetDialog_.IsOpen() && idleMs >= kIdleTimeoutMs)
        resetDialog_.Close();

    if (!serviceProvider_.getBoard().BootButtonPressed())
    {
        bootHeldTicks_ = 0;
        return;
    }
    if (bootHeldTicks_ >= kBootHoldTicks) return;   // this press already asked
    if (++bootHeldTicks_ < kBootHoldTicks) return;

    ESP_LOGI(TAG, "BOOT button pressed, asking whether to reset");
    resetDialog_.Show();
    // The button is not a touch, so the panel would stay dimmed without this.
    lv_display_trigger_activity(lvDisplay_);
}

void DisplayManager::TickCb(lv_timer_t *t)
{
    auto *self = static_cast<DisplayManager *>(lv_timer_get_user_data(t));
    self->ServicePanelResync();
    uint32_t idleMs = lv_display_get_inactive_time(self->lvDisplay_);
    self->ServiceBootButton(idleMs);

    // The site test is carried around untouched for minutes on end: it has to
    // stay bright, and dropping back to home would end the test mode halfway
    // through a walk.
    if (self->current_ == ScreenId::BleTest)
    {
        self->ServiceBacklight(0);
        return;
    }

    self->ServiceBacklight(idleMs);

    if (self->current_ == ScreenId::Home) return;
    if (idleMs < kIdleTimeoutMs) return;

    // Never leave a unit sitting in the service menu, unlocked, in someone's
    // hallway. Already inside the LVGL task, so load directly.
    ESP_LOGI(TAG, "Idle — returning to home screen");
    self->homeScreen_.Load();
    self->current_ = ScreenId::Home;
    self->ResyncPanel();
}
