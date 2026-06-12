#include "Screens.h"
#include "ServiceProvider.h"
#include "SettingsManager/SettingsManager.h"
#include "lvgl.h"
#include <cstring>
#include <cstdio>

// PIN entry gate in front of the configuration. Compares against the
// "device.pin" setting; when unset, the demo default "0000" applies.

namespace
{
constexpr uint32_t COL_BG     = 0x0B0E12;
constexpr uint32_t COL_CARD   = 0x1B212B;
constexpr uint32_t COL_BORDER = 0x2A323D;
constexpr uint32_t COL_MUTED  = 0x8A93A0;
constexpr uint32_t COL_WHITE  = 0xF2F5F8;
constexpr uint32_t COL_RED    = 0xE5484D;

constexpr int PIN_LEN = 4;

ServiceProvider *s_ctx = nullptr;
char s_entered[PIN_LEN + 1] = "";
lv_obj_t *s_dotsLabel = nullptr;
lv_obj_t *s_titleLabel = nullptr;

const char *KEYPAD_MAP[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, "",
};

void refreshDots()
{
    char text[32] = "";
    size_t entered = strlen(s_entered);
    for (int i = 0; i < PIN_LEN; i++)
        strcat(text, (size_t)i < entered ? "\xE2\x80\xA2 " : "_ ");  // • or _
    lv_label_set_text(s_dotsLabel, text);
}

void checkPin()
{
    char pin[16] = "";
    s_ctx->getSettingsManager().getString("device.pin", pin, sizeof(pin));
    if (pin[0] == '\0')
        snprintf(pin, sizeof(pin), "0000");  // demo default

    if (strcmp(s_entered, pin) == 0)
    {
        ShowGatewayScreen(*s_ctx);
        return;
    }

    s_entered[0] = '\0';
    refreshDots();
    lv_label_set_text(s_titleLabel, "Wrong PIN, try again");
    lv_obj_set_style_text_color(s_titleLabel, lv_color_hex(COL_RED), 0);
}

void onKey(lv_event_t *e)
{
    lv_obj_t *matrix = (lv_obj_t *)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(matrix);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE)
        return;
    const char *txt = lv_buttonmatrix_get_button_text(matrix, id);
    if (!txt)
        return;

    size_t len = strlen(s_entered);
    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
    {
        if (len > 0)
            s_entered[len - 1] = '\0';
    }
    else if (strcmp(txt, LV_SYMBOL_OK) == 0)
    {
        checkPin();
        return;
    }
    else if (len < PIN_LEN)
    {
        s_entered[len] = txt[0];
        s_entered[len + 1] = '\0';
    }

    refreshDots();
    if (strlen(s_entered) == PIN_LEN)
        checkPin();
}

void onBack(lv_event_t *) { ShowThermostatScreen(*s_ctx); }
} // namespace

void ShowPinScreen(ServiceProvider &ctx)
{
    s_ctx = &ctx;
    s_entered[0] = '\0';

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 8, 0);

    // ── Top bar: back · title ──
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 16, 0);

    lv_obj_t *back = lv_button_create(bar);
    lv_obj_set_size(back, 56, 48);
    lv_obj_set_style_bg_color(back, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_t *backIcon = lv_label_create(back);
    lv_label_set_text(backIcon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(backIcon, lv_color_hex(COL_WHITE), 0);
    lv_obj_center(backIcon);
    lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);

    s_titleLabel = lv_label_create(bar);
    lv_label_set_text(s_titleLabel, "Enter PIN");
    lv_obj_set_style_text_font(s_titleLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_titleLabel, lv_color_hex(COL_MUTED), 0);

    // ── PIN dots ──
    s_dotsLabel = lv_label_create(scr);
    lv_obj_set_style_text_font(s_dotsLabel, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_dotsLabel, lv_color_hex(COL_WHITE), 0);
    refreshDots();

    // ── Keypad ──
    lv_obj_t *matrix = lv_buttonmatrix_create(scr);
    lv_buttonmatrix_set_map(matrix, KEYPAD_MAP);
    lv_obj_set_width(matrix, 360);
    lv_obj_set_flex_grow(matrix, 1);
    lv_obj_set_style_bg_opa(matrix, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(matrix, 0, 0);
    lv_obj_set_style_pad_all(matrix, 0, 0);
    lv_obj_set_style_text_font(matrix, &lv_font_montserrat_28, 0);
    lv_obj_set_style_bg_color(matrix, lv_color_hex(COL_CARD), LV_PART_ITEMS);
    lv_obj_set_style_text_color(matrix, lv_color_hex(COL_WHITE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(matrix, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(matrix, lv_color_hex(COL_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(matrix, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(matrix, onKey, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}
