#include "Screens.h"
#include "ServiceProvider.h"
#include "ClimateManager/ClimateManager.h"
#include "BleManager/BleManager.h"
#include "fonts.h"   // font_temp_96, font_modes_40
#include "esp_log.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

// Thermostat screen:
//   - top bar: flame (heat output) · gateway name · bluetooth (link) · gear
//   - big ROOM temperature with a °C superscript, flanked by - / + buttons that
//     adjust the setpoint
//   - a "Set to NN.N°C" line (setpoint, in blue)
//   - four mode buttons with icons (Auto / Heat / Cool / Off)
//
// All state comes from the ClimateManager (which mirrors the gateway); a
// 250 ms LVGL timer polls it and repaints only when something changed.

namespace
{
// Font Awesome glyphs (UTF-8): F021 rotate, F06D fire, F2DC snowflake, F011 power.
struct ModeDef { const char *name; const char *icon; uint32_t color; ClimateMode mode; };
const ModeDef MODES[4] = {
    { "Auto", "\xEF\x80\xA1", 0x2BB24C, ClimateMode::Auto },
    { "Heat", "\xEF\x81\xAD", 0xF5662E, ClimateMode::Heating },
    { "Cool", "\xEF\x8B\x9C", 0x2D8CF6, ClimateMode::Cooling },
    { "Off",  "\xEF\x80\x91", 0x6B7280, ClimateMode::Off },
};
const char *ICON_FIRE = "\xEF\x81\xAD";

constexpr uint32_t COL_BG     = 0x0B0E12;
constexpr uint32_t COL_CARD   = 0x1B212B;
constexpr uint32_t COL_BORDER = 0x2A323D;
constexpr uint32_t COL_MUTED  = 0x8A93A0;
constexpr uint32_t COL_WHITE  = 0xF2F5F8;
constexpr uint32_t COL_BLUE   = 0x2D8CF6;
constexpr uint32_t COL_FLAME  = 0xF5662E;

constexpr float SETPOINT_STEP = 0.5f;

ServiceProvider *s_ctx = nullptr;

int modeIndex(ClimateMode mode)
{
    for (int i = 0; i < 4; ++i)
        if (MODES[i].mode == mode) return i;
    return 3;  // Off
}

struct Ui
{
    lv_obj_t *roomLabel = nullptr;
    lv_obj_t *setLabel = nullptr;
    lv_obj_t *flameIcon = nullptr;
    lv_obj_t *linkIcon = nullptr;
    lv_obj_t *gatewayLabel = nullptr;
    lv_obj_t *modeBtns[4] = {};
    lv_timer_t *timer = nullptr;

    // Last rendered values, to repaint only on change.
    int shownRoomTenths = -10000;
    int shownSetTenths = -10000;
    int shownModeIdx = -1;
    int shownHeating = -1;
    int shownLinked = -1;
    char shownGateway[32] = "";
};
Ui g;

void render()
{
    ClimateState s = s_ctx->getClimateManager().GetState();

    int roomTenths = (int)lroundf(s.roomTemp * 10.0f);
    int setTenths = (int)lroundf(s.setpoint * 10.0f);
    int modeIdx = modeIndex(s.mode);

    char gateway[32];
    s_ctx->getBleManager().GetGatewayName(gateway, sizeof(gateway));
    if (gateway[0] == '\0')
        snprintf(gateway, sizeof(gateway), "No gateway");

    if (roomTenths != g.shownRoomTenths)
    {
        g.shownRoomTenths = roomTenths;
        lv_label_set_text_fmt(g.roomLabel, "%d.%d", roomTenths / 10, abs(roomTenths) % 10);
    }

    if (setTenths != g.shownSetTenths)
    {
        g.shownSetTenths = setTenths;
        lv_label_set_text_fmt(g.setLabel, "%d.%d\xC2\xB0""C", setTenths / 10, abs(setTenths) % 10);
    }

    if ((int)s.heating != g.shownHeating)
    {
        g.shownHeating = s.heating;
        lv_obj_set_style_text_color(g.flameIcon, lv_color_hex(s.heating ? COL_FLAME : COL_CARD), 0);
    }

    if ((int)s.linked != g.shownLinked)
    {
        g.shownLinked = s.linked;
        lv_obj_set_style_text_color(g.linkIcon, lv_color_hex(s.linked ? COL_BLUE : COL_BORDER), 0);
    }

    if (strcmp(gateway, g.shownGateway) != 0)
    {
        snprintf(g.shownGateway, sizeof(g.shownGateway), "%s", gateway);
        lv_label_set_text(g.gatewayLabel, gateway);
    }

    if (modeIdx != g.shownModeIdx)
    {
        g.shownModeIdx = modeIdx;
        for (int i = 0; i < 4; ++i)
        {
            lv_obj_t *b = g.modeBtns[i];
            lv_obj_t *icon = lv_obj_get_child(b, 0);
            lv_obj_t *name = lv_obj_get_child(b, 1);
            bool active = (modeIdx == i);
            uint32_t fg = active ? COL_WHITE : COL_MUTED;
            if (active)
            {
                lv_obj_set_style_bg_color(b, lv_color_hex(MODES[i].color), 0);
                lv_obj_set_style_border_width(b, 0, 0);
            }
            else
            {
                lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
                lv_obj_set_style_border_width(b, 1, 0);
                lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
            }
            lv_obj_set_style_text_color(icon, lv_color_hex(fg), 0);
            lv_obj_set_style_text_color(name, lv_color_hex(fg), 0);
        }
    }
}

void pollTick(lv_timer_t *)
{
    render();

    // TEMP: touch diagnostics — logs raw indev presses while we debug the
    // unresponsive touchscreen. Remove once touch is confirmed working.
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED)
    {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        ESP_LOGI("TouchDebug", "pressed at %d,%d", (int)p.x, (int)p.y);
    }
}

void onPlus(lv_event_t *)  { s_ctx->getClimateManager().AdjustSetpoint(+SETPOINT_STEP); render(); }
void onMinus(lv_event_t *) { s_ctx->getClimateManager().AdjustSetpoint(-SETPOINT_STEP); render(); }
void onMode(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_ctx->getClimateManager().SetMode(MODES[i].mode);
    render();
}
void onGear(lv_event_t *) { ShowPinScreen(*s_ctx); }

void onScreenDelete(lv_event_t *)
{
    if (g.timer)
    {
        lv_timer_delete(g.timer);
        g.timer = nullptr;
    }
}

void strip(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
}

lv_obj_t *group(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    strip(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

lv_obj_t *adjustButton(lv_obj_t *parent, const char *sym, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 120, 120);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = makeLabel(b, sym, &lv_font_montserrat_48, COL_MUTED);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}
} // namespace

void ShowThermostatScreen(ServiceProvider &ctx)
{
    s_ctx = &ctx;
    g = Ui();

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 22, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 16, 0);

    // ── Top bar: flame · gateway name · link + gear ──
    lv_obj_t *bar = group(scr);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g.flameIcon = makeLabel(bar, ICON_FIRE, &font_modes_40, COL_CARD);
    g.gatewayLabel = makeLabel(bar, "", &lv_font_montserrat_20, COL_MUTED);

    lv_obj_t *right = group(bar);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 18, 0);

    g.linkIcon = makeLabel(right, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_20, COL_BORDER);

    lv_obj_t *gear = lv_button_create(right);
    lv_obj_set_size(gear, 48, 48);
    lv_obj_set_style_radius(gear, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_t *gearIcon = makeLabel(gear, LV_SYMBOL_SETTINGS, &lv_font_montserrat_20, COL_MUTED);
    lv_obj_center(gearIcon);
    lv_obj_add_event_cb(gear, onGear, LV_EVENT_CLICKED, nullptr);

    // ── Hero: [-]  big room temp + °C  [+]  ──
    lv_obj_t *hero = group(scr);
    lv_obj_set_width(hero, lv_pct(100));
    lv_obj_set_flex_grow(hero, 1);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    adjustButton(hero, "-", onMinus);

    // Centre block: temperature (with °C superscript) above the "Set to" line.
    lv_obj_t *centre = group(hero);
    lv_obj_set_size(centre, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(centre, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(centre, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(centre, 4, 0);

    lv_obj_t *tempRow = group(centre);
    lv_obj_set_size(tempRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tempRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tempRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(tempRow, 6, 0);
    g.roomLabel = makeLabel(tempRow, "--.-", &font_temp_96, COL_WHITE);
    lv_obj_t *unit = makeLabel(tempRow, "\xC2\xB0""C", &lv_font_montserrat_28, COL_WHITE);
    lv_obj_set_style_pad_top(unit, 14, 0);  // drop the °C a touch below the very top

    lv_obj_t *setRow = group(centre);
    lv_obj_set_size(setRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(setRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(setRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(setRow, 8, 0);
    makeLabel(setRow, "Set to", &lv_font_montserrat_20, COL_MUTED);
    g.setLabel = makeLabel(setRow, "--.-\xC2\xB0""C", &lv_font_montserrat_20, COL_BLUE);

    adjustButton(hero, "+", onPlus);

    // ── Mode buttons: icon + label ──
    lv_obj_t *modes = group(scr);
    lv_obj_set_width(modes, lv_pct(100));
    lv_obj_set_height(modes, 104);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modes, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(modes, 14, 0);

    for (int i = 0; i < 4; ++i)
    {
        lv_obj_t *b = lv_button_create(modes);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, lv_pct(100));
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, 4, 0);

        makeLabel(b, MODES[i].icon, &font_modes_40, COL_MUTED);   // child 0: icon
        makeLabel(b, MODES[i].name, &lv_font_montserrat_20, COL_MUTED); // child 1: name

        lv_obj_add_event_cb(b, onMode, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g.modeBtns[i] = b;
    }

    render();
    g.timer = lv_timer_create(pollTick, 250, nullptr);
    lv_obj_add_event_cb(scr, onScreenDelete, LV_EVENT_DELETE, nullptr);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}
