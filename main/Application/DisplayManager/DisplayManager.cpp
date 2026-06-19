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
    InitKnob();   // best-effort; no-op on boards without a rotary knob

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

#ifdef BOARD_PANEL_RGB
    // ── RGB-direct-framebuffer panels (Sunton, Elecrow, VIEWE) ──────
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
#else
    // ── Command-driven panels (WT-SC01 Plus: ST7796 over i80) ───────
    // Pixels are pushed via esp_lcd_panel_draw_bitmap, so lvgl_port needs both
    // the panel handle and the panel-IO handle (where it hooks the flush-done
    // callback). Double-buffered partial draw buffers in internal DMA RAM let
    // LVGL render the next chunk while the i80 DMA flushes the previous one.
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = display_.io();
    disp_cfg.panel_handle = display_.panel();
    disp_cfg.buffer_size = Display::Width() * 40;
    disp_cfg.double_buffer = true;
    disp_cfg.hres = Display::Width();
    disp_cfg.vres = Display::Height();
    disp_cfg.monochrome = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.buff_spiram = false;
    // 8-bit i80 bus sends pixels high-byte first; LVGL RGB565 is little-endian.
    disp_cfg.flags.swap_bytes = true;
    disp_cfg.flags.full_refresh = false;
    // Landscape: the WT-SC01 Plus panel is native portrait 320x480; swap X/Y so
    // it presents as 480x320. esp_lvgl_port owns the panel's swap_xy/mirror
    // state (it re-applies these on the panel itself), so the rotation MUST be
    // set here — not via esp_lcd_panel_swap_xy() in Display.h, which the port
    // would clobber. Matches the proven ST7796 config in the WT32-SC01 sibling
    // project: swap_xy only, no mirror. If the image comes out mirrored or
    // 180°-rotated, set mirror_x/mirror_y here (NOT in Display.h).
    disp_cfg.rotation.swap_xy = true;
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = false;

    lvDisplay_ = lvgl_port_add_disp(&disp_cfg);
    if (lvDisplay_ == nullptr)
    {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        return false;
    }
#endif
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
        ESP_LOGI(TAG, "Touch ready");
}

void DisplayManager::InitKnob()
{
#ifdef BOARD_HAS_KNOB
    if (!knob_.Init())
    {
        ESP_LOGW(TAG, "Knob unavailable — continuing without it");
        return;
    }

    // Register the encoder indev and a default group, before BuildUi() so that
    // screens adding their widgets to lv_group_get_default() become navigable.
    if (!lvgl_port_lock(0))
    {
        ESP_LOGW(TAG, "Could not lock LVGL to register knob");
        return;
    }
    lv_indev_t *indev = knob_.CreateLvglIndev();
    lv_indev_set_display(indev, lvDisplay_);
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(indev, group);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Rotary knob ready");
#endif
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
