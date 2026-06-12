#include "Screens.h"
#include "ServiceProvider.h"
#include "BleManager/BleManager.h"
#include "lvgl.h"
#include <cstring>
#include <cstdio>

// Gateway selection screen: scans for nearby KC1245 gateways (they advertise
// the KC thermostat service with their gateway name + id) and connects to the
// one the user taps. The selection is persisted by the BleManager.

namespace
{
constexpr uint32_t COL_BG     = 0x0B0E12;
constexpr uint32_t COL_CARD   = 0x1B212B;
constexpr uint32_t COL_BORDER = 0x2A323D;
constexpr uint32_t COL_MUTED  = 0x8A93A0;
constexpr uint32_t COL_WHITE  = 0xF2F5F8;
constexpr uint32_t COL_BLUE   = 0x2D8CF6;

constexpr int MAX_RESULTS = 8;

ServiceProvider *s_ctx = nullptr;
lv_obj_t *s_list = nullptr;
lv_obj_t *s_hint = nullptr;
lv_timer_t *s_timer = nullptr;
GatewayInfo s_results[MAX_RESULTS];
int s_resultCount = 0;
uint32_t s_lastSignature = 0;

void onSelect(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_resultCount)
        return;
    s_ctx->getBleManager().SelectGateway(s_results[idx]);
    ShowThermostatScreen(*s_ctx);
}

void rebuildList()
{
    char selectedAddr[18];
    s_ctx->getBleManager().GetGatewayAddr(selectedAddr, sizeof(selectedAddr));

    lv_obj_clean(s_list);
    for (int i = 0; i < s_resultCount; i++)
    {
        const GatewayInfo &gw = s_results[i];

        char text[64];
        snprintf(text, sizeof(text), "%s\n%s   %d dBm",
                 gw.name[0] ? gw.name : "(no name)", gw.addrStr, gw.rssi);

        bool selected = (strcmp(gw.addrStr, selectedAddr) == 0);
        lv_obj_t *btn = lv_list_add_button(s_list,
                                           selected ? LV_SYMBOL_OK : LV_SYMBOL_BLUETOOTH, text);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(selected ? COL_BLUE : COL_WHITE), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(COL_BORDER), 0);
        lv_obj_add_event_cb(btn, onSelect, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    if (s_hint)
        lv_obj_set_style_opa(s_hint, s_resultCount == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

// Rebuild only when the result set meaningfully changes (count or names),
// not on every RSSI fluctuation — a mid-tap rebuild would steal the press.
uint32_t signature()
{
    uint32_t sig = (uint32_t)s_resultCount * 7919;
    for (int i = 0; i < s_resultCount; i++)
        for (const char *p = s_results[i].name; *p; p++)
            sig = sig * 31 + (uint8_t)*p;
    return sig;
}

void pollTick(lv_timer_t *)
{
    s_resultCount = s_ctx->getBleManager().GetScanResults(s_results, MAX_RESULTS);
    uint32_t sig = signature();
    if (sig != s_lastSignature)
    {
        s_lastSignature = sig;
        rebuildList();
    }
}

void onBack(lv_event_t *)
{
    s_ctx->getBleManager().StopScan();
    ShowThermostatScreen(*s_ctx);
}

void onScreenDelete(lv_event_t *)
{
    if (s_timer)
    {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    if (s_ctx)
        s_ctx->getBleManager().StopScan();
}
} // namespace

void ShowGatewayScreen(ServiceProvider &ctx)
{
    s_ctx = &ctx;
    s_resultCount = 0;
    s_lastSignature = 0;

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 12, 0);

    // ── Top bar: back · title · spinner ──
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *back = lv_button_create(bar);
    lv_obj_set_size(back, 56, 48);
    lv_obj_set_style_bg_color(back, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_t *backIcon = lv_label_create(back);
    lv_label_set_text(backIcon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(backIcon, lv_color_hex(COL_WHITE), 0);
    lv_obj_center(backIcon);
    lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Select gateway");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_WHITE), 0);

    lv_obj_t *spinner = lv_spinner_create(bar);
    lv_obj_set_size(spinner, 32, 32);

    // ── Result list ──
    s_list = lv_list_create(scr);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);

    s_hint = lv_label_create(scr);
    lv_label_set_text(s_hint, "Searching for gateways...");
    lv_obj_set_style_text_color(s_hint, lv_color_hex(COL_MUTED), 0);

    s_ctx->getBleManager().StartScan();
    s_timer = lv_timer_create(pollTick, 500, nullptr);
    lv_obj_add_event_cb(scr, onScreenDelete, LV_EVENT_DELETE, nullptr);

    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}
