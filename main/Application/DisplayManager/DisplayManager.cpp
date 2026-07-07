#include "DisplayManager.h"
#include "ClimateManager/ClimateManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"
#include "Board.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <cstdio>

extern "C" const lv_font_t font_temp_96;

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

    if (serviceProvider_.getBoard().GetPanel() == nullptr)
    {
        ESP_LOGW(TAG, "No panel (headless) — UI disabled");
        return;
    }
    if (!InitLvgl()) return;

    // Touch (best-effort — display still works without it).
    if (serviceProvider_.getBoard().GetTouch())
    {
        lvgl_port_touch_cfg_t tcfg = {};
        tcfg.disp = lvDisplay_;
        tcfg.handle = serviceProvider_.getBoard().GetTouch();
        if (lvgl_port_add_touch(&tcfg) == nullptr)
            ESP_LOGW(TAG, "lvgl_port_add_touch failed — touch disabled");
    }

    BuildUi();
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

void DisplayManager::BuildUi()
{
    if (!lvgl_port_lock(0)) return;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    stateLabel_ = lv_label_create(scr);
    lv_obj_set_style_text_color(stateLabel_, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(stateLabel_, &lv_font_montserrat_28, 0);
    lv_label_set_text(stateLabel_, "");
    lv_obj_align(stateLabel_, LV_ALIGN_TOP_MID, 0, 60);

    bigLabel_ = lv_label_create(scr);
    lv_obj_set_style_text_color(bigLabel_, lv_color_white(), 0);
    lv_obj_set_style_text_font(bigLabel_, &font_temp_96, 0);
    lv_obj_align(bigLabel_, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *minus = lv_button_create(scr);
    lv_obj_set_size(minus, 200, 130);
    lv_obj_align(minus, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_add_event_cb(minus, MinusCb, LV_EVENT_CLICKED, this);
    lv_obj_t *ml = lv_label_create(minus);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_48, 0);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    lv_obj_t *plus = lv_button_create(scr);
    lv_obj_set_size(plus, 200, 130);
    lv_obj_align(plus, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_add_event_cb(plus, PlusCb, LV_EVENT_CLICKED, this);
    lv_obj_t *pl = lv_label_create(plus);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_48, 0);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    lvgl_port_unlock();

    ShowRoomTemp();
    lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

void DisplayManager::ShowRoomTemp()
{
    float t = 0;
    bool valid = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(t);
    char buf[16];
    if (valid) snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", t);   // UTF-8 degree
    else       snprintf(buf, sizeof(buf), "--.-\xC2\xB0");

    if (!lvgl_port_lock(0)) return;
    lv_label_set_text(stateLabel_, "");
    lv_label_set_text(bigLabel_, buf);
    lvgl_port_unlock();
    showingSetpoint_ = false;
}

void DisplayManager::OnNudge(float deltaC)
{
    serviceProvider_.getClimateManager().NudgeSetpoint(deltaC);
    float sp = serviceProvider_.getClimateManager().GetUserSetpoint();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", sp);

    if (lvgl_port_lock(0))
    {
        lv_label_set_text(stateLabel_, "SET");
        lv_label_set_text(bigLabel_, buf);
        if (revertTimer_) lv_timer_reset(revertTimer_);
        else revertTimer_ = lv_timer_create(RevertTimerCb, kRevertMs, this);
        lvgl_port_unlock();
    }
    showingSetpoint_ = true;
}

void DisplayManager::RefreshTimerCb(lv_timer_t *t)
{
    auto *self = static_cast<DisplayManager *>(lv_timer_get_user_data(t));
    if (!self->showingSetpoint_) self->ShowRoomTemp();   // don't clobber the setpoint view
}

void DisplayManager::RevertTimerCb(lv_timer_t *t)
{
    auto *self = static_cast<DisplayManager *>(lv_timer_get_user_data(t));
    lv_timer_delete(t);
    self->revertTimer_ = nullptr;
    self->ShowRoomTemp();
}

void DisplayManager::MinusCb(lv_event_t *e)
{
    static_cast<DisplayManager *>(lv_event_get_user_data(e))->OnNudge(-0.5f);
}

void DisplayManager::PlusCb(lv_event_t *e)
{
    static_cast<DisplayManager *>(lv_event_get_user_data(e))->OnNudge(+0.5f);
}
