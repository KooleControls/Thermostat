#pragma once

#include "Screen.h"
#include "ServiceProvider.h"

// The service menu behind the gear: a scrollable column of rows, each leading to
// its own purpose-built screen.
//
// Rows for features that don't exist yet are added disabled — visible so the
// menu reads as complete, unresponsive so there is no dead navigation. Enabling
// one is a two-line change in Build() plus the screen it points at.
class SettingsMenuScreen final : public Screen
{
public:
    SettingsMenuScreen(ServiceProvider& serviceProvider, Navigator& navigator)
        : serviceProvider_(serviceProvider), navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;   // refreshes the WiFi row's summary

private:
    /// One menu row leading to `target`. Returns the right-hand summary label so
    /// the caller can keep it and update it later.
    lv_obj_t* AddRow(lv_obj_t* list, const char* icon, const char* text, ScreenId target);
    void AddPendingRow(lv_obj_t* list, const char* icon, const char* text);

    static void RowCb(lv_event_t* e);
    static void CloseCb(lv_event_t* e);

    lv_obj_t* MakeRow(lv_obj_t* list, const char* icon, const char* text,
                      const char* trailing, lv_obj_t** trailingLabel);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    lv_obj_t* wifiSummary_ = nullptr;   // SSID / "not connected" on the WiFi row
};
