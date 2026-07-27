#include "InfoScreen.h"
#include "NetworkManager/NetworkManager.h"
#include "SystemManager/SystemManager.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <cstdio>

void InfoScreen::Build(lv_obj_t* root)
{
    lv_obj_t* back = AddHeader(root, "Info", LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back, BackCb, LV_EVENT_CLICKED, this);

    lv_obj_t* list = lv_obj_create(root);
    lv_obj_set_size(list, LV_PCT(100), 480 - UiTheme::HeaderH);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, UiTheme::Pad, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    AddRow(list, Name,    "Device");
    AddRow(list, Version, "Firmware");
    AddRow(list, Mode,    "WiFi");
    AddRow(list, Ip,      "IP");
    AddRow(list, Mac,     "MAC");
    AddRow(list, Uptime,  "Uptime");
    AddRow(list, Heap,    "Free heap");
}

void InfoScreen::AddRow(lv_obj_t* list, Row row, const char* label)
{
    lv_obj_t* line = lv_obj_create(list);
    lv_obj_set_size(line, LV_PCT(100), 48);
    lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* key = lv_label_create(line);
    lv_obj_set_style_text_font(key, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(key, UiTheme::TextDim(), 0);
    lv_label_set_text(key, label);
    lv_obj_align(key, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* value = lv_label_create(line);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(value, UiTheme::Text(), 0);
    lv_label_set_text(value, "-");
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);

    values_[row] = value;
}

void InfoScreen::OnShow()
{
    char buf[64];

    serviceProvider_.getSystemManager().GetDeviceName(buf, sizeof(buf));
    lv_label_set_text(values_[Name], buf);

    lv_label_set_text(values_[Version], esp_app_get_description()->version);

    NetworkManager& net = serviceProvider_.getNetworkManager();
    lv_label_set_text(values_[Mode], net.IsAccessPoint() ? "Access point" : "Station");

    NetworkStatus status = net.wifi().getStatus();
    if (status.has_ipv4)
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&status.ipv4.ip));
    else
        snprintf(buf, sizeof(buf), "no address");
    lv_label_set_text(values_[Ip], buf);

    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             status.mac[0], status.mac[1], status.mac[2],
             status.mac[3], status.mac[4], status.mac[5]);
    lv_label_set_text(values_[Mac], buf);

    int64_t seconds = esp_timer_get_time() / 1000000;
    snprintf(buf, sizeof(buf), "%lldd %02lld:%02lld:%02lld",
             seconds / 86400, (seconds % 86400) / 3600, (seconds % 3600) / 60, seconds % 60);
    lv_label_set_text(values_[Uptime], buf);

    snprintf(buf, sizeof(buf), "%u kB", (unsigned)(esp_get_free_heap_size() / 1024));
    lv_label_set_text(values_[Heap], buf);
}

void InfoScreen::BackCb(lv_event_t* e)
{
    static_cast<InfoScreen*>(lv_event_get_user_data(e))->navigator_.Go(ScreenId::Settings);
}
