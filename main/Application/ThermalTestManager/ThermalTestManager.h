#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Power-state switch, for the self-heating question.
//
// The room sensor shares an enclosure with a backlit 4" panel, a continuously
// scanning RGB refresh path and a 240 MHz SoC, and reads several degrees above
// the actual room. This manager turns those off in stages so the difference can
// be measured.
//
// It records nothing, on purpose. The measurement is the gateway's own log: the
// thermostat reports room temperature over OpenTherm, and a second gateway
// running the old thermostat is the reference. Every lever here therefore keeps
// the OT link alive — that is the instrument. Light sleep is the one thing
// deliberately absent: it starves the OT task, so it would blind the gateway and
// measure nothing. Stopping the WiFi radio is fine by that rule (it costs the
// web UI, not OpenTherm) and is offered.
//
// Levers, coarsest first (see Board for the hardware detail):
//   backlight   LEDC duty on the LED string (0 = off). Biggest contributor.
//   panel       the RGB refresh clock, and finally the ST7701's reset line. The
//               panel cannot be switched off through esp_lcd on this board — its
//               command pins are the OpenTherm UART now, so disp_on_off() would
//               corrupt the link. Holding reset is ONE-WAY: only a reboot brings
//               the display back.
//   cpu freq    esp_pm_configure with max == min; needs CONFIG_PM_ENABLE, and
//               reports itself unavailable rather than failing without it.
//   radio       WiFi stopped outright. Costs the web UI (reboot to get it back)
//               but not OpenTherm, so the gateway keeps logging — which is what
//               makes the SoC's own contribution measurable at all.
//
// The last two are the ESP's own heat, and are settable on their own rather than
// only as part of the panel ladder: the board vendor's point is that the SoC
// contributes as well, and attributing that needs each lever moved by itself.
//
// Only the brightness is persisted, and only because a multi-hour comparison run
// must survive a reboot: coming back at 100 % would quietly ruin the run, and the
// gateway's log cannot tell a reboot from a real temperature rise. The boot log
// says which value was restored, so a restart is still visible on the console.
// Everything else lands in Baseline on boot.
// ──────────────────────────────────────────────────────────────

enum class ThermalMode : uint8_t
{
    Baseline = 0,   // as shipped: backlight 100 %, full refresh, 240 MHz
    DarkScreen,     // backlight off
    PanelIdle,      // + refresh clock slowed to a crawl
    PanelOff,       // + ST7701 held in reset, CPU at 80 MHz (one-way)
    Custom,         // brightness set on its own
};

class ThermalTestManager
{
    static constexpr const char *TAG = "ThermalTest";

    static constexpr int      SampleSec = 30;
    static constexpr uint32_t IdlePclkHz = 1000000;    // 10 % of the board default
    static constexpr uint32_t OffPclkHz = 100000;      // ~0.4 fps, near-zero DMA
    static constexpr int      LowCpuMhz = 80;
    static constexpr int      FullCpuMhz = 240;

public:
    explicit ThermalTestManager(ServiceProvider &serviceProvider);

    ThermalTestManager(const ThermalTestManager &) = delete;
    ThermalTestManager &operator=(const ThermalTestManager &) = delete;
    ThermalTestManager(ThermalTestManager &&) = delete;
    ThermalTestManager &operator=(ThermalTestManager &&) = delete;

    void Init();

private:
    // A mode is exactly these three lever positions, plus whether it kills the
    // panel outright. pclkHz == 0 means "the board's own default".
    struct Levers
    {
        uint8_t  backlight;
        uint32_t pclkHz;
        int      cpuMhz;
        bool     panelReset;
    };

    static const char *ModeName(ThermalMode mode);
    static bool        ParseMode(const char *name, ThermalMode &out);
    static Levers      LeversFor(ThermalMode mode);

    void ApplyMode(ThermalMode mode);
    void ApplyLevers(const Levers &levers);
    bool SetCpuMhz(int mhz);

    void Loop();
    void LogSample();

    void WriteStatus(Stream &out);
    void Cmd_ThermalStatus(Stream &in, Stream &out);
    void Cmd_ThermalSet(Stream &in, Stream &out);

    inline static CommandEntry commands_[] = {
        { "thermalStatus", &InvokeCommand<&ThermalTestManager::Cmd_ThermalStatus> },
        { "thermalSet",    &InvokeCommand<&ThermalTestManager::Cmd_ThermalSet> },
    };

    // The one persisted value — see the note at the top of this file. 100 % is
    // the default, so a fresh unit behaves exactly as it always did.
    inline static UInt32Setting backlightSetting_{ "thermal.bl", "Backlight (%)", 100 };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    // Set by the command, acted on by the task: stopping WiFi tears down the
    // socket the reply travels over.
    bool stopRadioRequested_ = false;

    ThermalMode mode_ = ThermalMode::Baseline;
    Levers      levers_{ 100, 0, FullCpuMhz, false };
    int         cpuMhzActual_ = FullCpuMhz;
    bool        cpuControl_ = false;   // false when the build has no CONFIG_PM_ENABLE
    int64_t     stateEnteredUs_ = 0;
};
