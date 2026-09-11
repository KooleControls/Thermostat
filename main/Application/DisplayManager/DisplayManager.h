#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Screen.h"
#include "PinGate.h"
#include "HomeScreen.h"
#include "PinScreen.h"
#include "SettingsMenuScreen.h"
#include "WifiScreen.h"
#include "BleScreen.h"
#include "InfoScreen.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include "lvgl.h"

// Owns LVGL (via esp_lvgl_port) and is the navigation shell: it holds every
// screen by value, switches between them, guards the service menu with a PIN,
// and drops back to the home screen when the panel is left untouched.
//
// Screens never reach the shell directly — they get the Navigator interface, so
// adding one is: write the class, add a ScreenId, add a member, add a case in
// Go(). Headless-safe: no panel means Init() and Go() are no-ops.
class DisplayManager final : public Navigator
{
    static constexpr const char* TAG = "DisplayManager";

    /// The housekeeping tick drives both the menu timeout and the backlight.
    /// It runs ten times a second not because either deadline is tight, but
    /// because the *wake* is: a panel that takes a second to brighten after a
    /// touch feels broken, and the tick is a counter read and a comparison.
    static constexpr uint32_t kTickMs = 100;
    static constexpr uint32_t kIdleTimeoutMs = 60000;   // menu → home when untouched

    /// Backstop poll for the touch controller once INT drives the reads.
    /// Not the press path — see ArmTouchBackstop().
    static constexpr uint32_t kTouchBackstopMs = 100;

public:
    explicit DisplayManager(ServiceProvider& serviceProvider);

    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;
    DisplayManager(DisplayManager&&) = delete;
    DisplayManager& operator=(DisplayManager&&) = delete;

    void Init();

    /// Switch screens. Safe to call from any task — takes the LVGL lock itself.
    /// ScreenId::Pin resolves to the settings menu when no PIN is stored, so the
    /// gate policy stays here with the PinGate rather than in the home screen.
    void Go(ScreenId id) override;

    /// Re-read the palette from the theme setting and rebuild every screen.
    /// Safe to call from any task and from inside an LVGL event callback on the
    /// screen being rebuilt — the port lock is recursive and LVGL marks an
    /// in-flight event dead when its target is destroyed.
    void Restyle() override;

private:
    bool InitLvgl();
    static void ArmTouchBackstop(lv_indev_t* indev);
    Screen* Resolve(ScreenId id);
    void ServiceBacklight(uint32_t idleMs);
    static void TickCb(lv_timer_t* t);

    static const char* ScreenName(ScreenId id);
    static bool ParseScreen(const char* name, ScreenId& out);

    /// Every id exactly once — the name lookup walks it, and so does the
    /// rebuild, which must not miss a screen or it would keep a stale palette.
    static constexpr ScreenId kAllScreens[] = {
        ScreenId::Home, ScreenId::Pin, ScreenId::Settings,
        ScreenId::Wifi, ScreenId::Ble, ScreenId::Info,
    };

    /// `uiGo` — drive navigation from a bench client instead of a fingertip.
    ///   {"screen":"home"|"pin"|"settings"|"wifi"|"ble"|"info"}
    /// Reports the screen actually shown, which is not always the one asked
    /// for: Pin resolves to Settings when no PIN is stored. Omitting "screen"
    /// just reads the current one back.
    ///
    /// Exists because a screen change is the heaviest thing the panel does — a
    /// full-screen redraw — and measuring it had meant asking someone to tap
    /// the glass at the right moment. Also the only way to exercise the UI on
    /// a headless or remote unit.
    RequestError Cmd_UiGo(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "ui", "go", &InvokeCommand<&DisplayManager::Cmd_UiGo> },
    };

    // The panel rests dim and comes up full when someone is at it. The motive
    // is thermal as much as it is power: the backlight is the largest single
    // contributor to the self-heating that skews the room sensor, so the state
    // the unit spends its life in is the one the calibration has to hold for.
    // NVS keys are capped at 15 characters.
    inline static UInt32Setting dimPercent_{ "ui.dimPct",  "Backlight Dim (%)",   30 };
    inline static UInt32Setting fullPercent_{ "ui.fullPct", "Backlight Full (%)", 100 };
    inline static UInt32Setting dimAfterS_{ "ui.dimSec",   "Backlight Dim After (s)", 30 };

    // The theme setting itself lives on the settings screen that carries its
    // toggle; the shell only registers it and reads it, because the palette has
    // to be chosen before the first screen is built.

    ServiceProvider& serviceProvider_;
    InitState initState_;
    lv_display_t* lvDisplay_ = nullptr;

    PinGate pinGate_;

    // Declaration order matters: these take serviceProvider_ and *this.
    HomeScreen homeScreen_{serviceProvider_, *this};
    PinScreen pinScreen_{pinGate_, *this};
    SettingsMenuScreen settingsScreen_{serviceProvider_, *this};
    WifiScreen wifiScreen_{serviceProvider_, *this};
    BleScreen bleScreen_{serviceProvider_, *this};
    InfoScreen infoScreen_{serviceProvider_, *this};

    ScreenId current_ = ScreenId::Home;

    /// Which of the two levels is currently on the panel. Tracked so the tick
    /// only writes LEDC on a transition rather than ten times a second.
    bool backlightFull_ = true;
};
