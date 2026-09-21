#pragma once

#include "Screen.h"
#include "ServiceProvider.h"
#include "TypedSettings.h"
#include "UiTheme.h"

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

    /// Lives here because this is the screen that owns the toggle, but it is
    /// the shell that registers it and reads it at boot — the palette has to be
    /// chosen before the first screen is built. NVS keys are capped at 15
    /// characters. Default dark: it is what every unit has shipped with.
    inline static BoolSetting lightTheme_{ "ui.light", "Light Theme", false };

    static UiTheme::Mode ThemeMode()
    {
        return lightTheme_.Get() ? UiTheme::Mode::Light : UiTheme::Mode::Dark;
    }

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;   // refreshes the WiFi row's summary

private:
    /// One menu row leading to `target`. Returns the subtitle label so the
    /// caller can keep it and update it later — the second line is where a row
    /// says what it currently is, not just what it is called.
    lv_obj_t* AddRow(lv_obj_t* list, const char* icon, const char* text,
                     const char* subtitle, ScreenId target);
    void AddPendingRow(lv_obj_t* list, const char* icon, const char* text);

    /// A row carrying a switch instead of a chevron. The switch is display-only
    /// and the *row* is the control: a 76 px row is the touch target, and one
    /// event path means a tap cannot be counted twice.
    void AddToggleRow(lv_obj_t* list, const char* icon, const char* text,
                      const char* subtitle, bool on);

    static void RowCb(lv_event_t* e);
    static void CloseCb(lv_event_t* e);
    static void ThemeCb(lv_event_t* e);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    lv_obj_t* wifiSummary_ = nullptr;   // SSID / "not connected" on the WiFi row
};
