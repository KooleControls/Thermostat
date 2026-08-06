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
    static constexpr uint32_t kIdleTickMs = 1000;
    static constexpr uint32_t kIdleTimeoutMs = 60000;   // menu → home when untouched

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

private:
    bool InitLvgl();
    Screen* Resolve(ScreenId id);
    static void IdleTimerCb(lv_timer_t* t);

    static const char* ScreenName(ScreenId id);
    static bool ParseScreen(const char* name, ScreenId& out);

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
};
