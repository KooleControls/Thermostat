#pragma once

#include "Screen.h"
#include "ServiceProvider.h"
#include "TypedSettings.h"
#include "UiTheme.h"

// The service menu behind the gear: a scrollable column of rows, each leading to
// its own purpose-built screen.
//
// Only rows that go somewhere. A row for a feature that does not exist yet —
// firmware update was the one — is a promise the menu cannot keep, and a
// disabled row still costs the reader a look every time they open this screen.
// Adding one back is a line in Build() plus the screen it points at.
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
    /// The WiFi row's second line: which network, and the address on it.
    ///
    /// On a timer as well as on show, because the interesting transitions all
    /// happen while somebody is looking at this screen — the association
    /// completes, then DHCP lands a second or two later. A row that only
    /// refreshes on load would sit on "connecting..." until the reader gave up
    /// and navigated away, which is exactly when it would have changed.
    void RefreshWifiRow();

    static constexpr uint32_t kRefreshMs = 1000;
    static void RefreshTimerCb(lv_timer_t* t);
    lv_timer_t* refreshTimer_ = nullptr;

    /// One menu row leading to `target`. Returns the subtitle label so the
    /// caller can keep it and update it later — the second line is where a row
    /// says what it currently is, not just what it is called.
    lv_obj_t* AddRow(lv_obj_t* list, const char* icon, const char* text,
                     const char* subtitle, ScreenId target);

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
