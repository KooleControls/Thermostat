#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Self-heating measurement rig.
//
// The AHT20 shares an enclosure with a backlit 4" panel, a continuously
// scanning RGB refresh path and a 240 MHz SoC, and consequently reads several
// degrees above the actual room. This manager exists to find out how much each
// of those contributes: it switches the board between defined power states and
// logs the sensor while it settles.
//
// It is a measurement rig, not shipping policy — the eventual dim-on-idle
// behaviour belongs in DisplayManager, and the offset decision that follows
// from these numbers is a separate question (docs/backlog/2026-07-27-backlight-
// dimming.md). Nothing here restores the backlight on touch, on purpose: a
// stray hand must not perturb a running measurement.
//
// Levers, coarsest first — see Board for the hardware detail:
//   backlight   LEDC duty on the LED string (0 = fully off). Biggest effect.
//   panel pclk  RGB refresh clock. Lowering it cuts the DMA/PSRAM traffic that
//               scans the framebuffer out to the panel. The panel cannot be
//               switched off outright on this board (its SPI lines are now the
//               OpenTherm UART), so a crawling clock is the available proxy.
//   cpu freq    esp_pm_configure with max == min. Needs CONFIG_PM_ENABLE; when
//               the build lacks it the lever reports itself unavailable rather
//               than failing.
//
// Ambient drift over an afternoon is larger than the effect being measured, so
// a single before/after run proves nothing. The rig therefore cycles between
// two modes on a fixed dwell (default 90 min, ~3x the enclosure's thermal time
// constant), which lets the analysis compare like with like. Mode and cycle
// settings persist, so an unattended run survives a reboot.
//
// Every sample is one CSV line on the console, prefixed THERMAL, and is kept in
// a ring buffer for the web UI and for backfilling a poller that missed some.
//
// Floor mode answers a different question from the graded modes: not "what does
// each part cost" but "how cold can this sensor ever read" — the physical floor,
// with every part of the product deliberately broken. It stops the radio, holds
// the panel in reset and duty-cycles the CPU through light sleep for a set
// number of minutes, then brings WiFi back and reports. Nothing about it is
// shippable behaviour; it exists so we know whether the sensor position can
// work at all before spending effort on offsets.
// ──────────────────────────────────────────────────────────────

enum class ThermalMode : uint8_t
{
    Baseline = 0,   // as shipped: backlight 100 %, full pclk, 240 MHz
    DarkScreen,     // backlight fully off
    PanelIdle,      // + refresh clock slowed to a crawl
    LowPower,       // + CPU at 80 MHz
    Floor,          // everything off that can be: see RunFloor()
    Custom,         // levers set individually over the command surface
};

class ThermalTestManager
{
    static constexpr const char *TAG = "ThermalTest";

    static constexpr int      TickMs = 1000;
    static constexpr uint32_t IdlePclkHz = 1000000;   // 10 % of the board default
    static constexpr int      LowPowerCpuMhz = 80;
    static constexpr int      FullCpuMhz = 240;
    static constexpr int      MaxSamples = 240;       // 2 h at the default cadence
    static constexpr uint32_t DeltaWindowS = 1800;    // "change over the last 30 min"

    static constexpr uint32_t MinDwellMin = 1;
    static constexpr uint32_t MaxDwellMin = 720;
    static constexpr uint32_t MinSampleSec = 5;
    static constexpr uint32_t MaxSampleSec = 600;

    // Floor mode is time-boxed because it is also unreachable: with the radio
    // stopped, the only way out is the clock running out.
    static constexpr uint32_t MinFloorMin = 1;
    static constexpr uint32_t MaxFloorMin = 240;
    static constexpr uint32_t FloorPclkHz = 100000;   // ~0.4 fps, near-zero DMA

public:
    explicit ThermalTestManager(ServiceProvider &serviceProvider);

    ThermalTestManager(const ThermalTestManager &) = delete;
    ThermalTestManager &operator=(const ThermalTestManager &) = delete;
    ThermalTestManager(ThermalTestManager &&) = delete;
    ThermalTestManager &operator=(ThermalTestManager &&) = delete;

    void Init();

private:
    struct Sample
    {
        uint32_t uptimeS = 0;
        float    room = 0.0f;
        float    humidity = 0.0f;
        float    die = 0.0f;
        uint8_t  mode = 0;
        uint8_t  backlight = 0;
        bool     roomValid = false;
        bool     humidityValid = false;
        bool     dieValid = false;
    };

    // A mode is exactly these three lever positions. pclkHz == 0 means "the
    // board's own default", so the table doesn't have to know the panel timing.
    struct Levers
    {
        uint8_t  backlight;
        uint32_t pclkHz;
        int      cpuMhz;
    };

    static const char *ModeName(ThermalMode mode);
    static bool        ParseMode(const char *name, ThermalMode &out);
    static Levers      LeversFor(ThermalMode mode);

    void ApplyMode(ThermalMode mode);
    void ApplyLevers(const Levers &levers);
    bool SetCpuMhz(int mhz);
    void PersistConfig();

    void Loop();
    /// `direct` reads the sensor itself instead of the cached value — needed
    /// during the sleep soak, where the RTOS tick has come loose from wall clock
    /// and every cached reading looks stale.
    void TakeSample(bool direct = false);
    void LogSample(const Sample &s);

    /// The floor run: radio off, panel in reset, CPU duty-cycled through light
    /// sleep for floorMin_ minutes, then WiFi back. Runs on the manager's own
    /// task (never on a command's task — the reply has to get out before the
    /// radio dies).
    void RunFloor();

    /// Change in `value` over the last `windowS` seconds, using the oldest
    /// sample still inside the window. Call under mutex_.
    bool DeltaOver(uint32_t windowS, float Sample::*value, bool Sample::*valid,
                   float &out) const;

    void WriteStatus(Stream &out);
    void Cmd_ThermalStatus(Stream &in, Stream &out);
    void Cmd_ThermalSet(Stream &in, Stream &out);
    void Cmd_ThermalLog(Stream &in, Stream &out);

    inline static CommandEntry commands_[] = {
        { "thermalStatus", &InvokeCommand<&ThermalTestManager::Cmd_ThermalStatus> },
        { "thermalSet",    &InvokeCommand<&ThermalTestManager::Cmd_ThermalSet> },
        { "thermalLog",    &InvokeCommand<&ThermalTestManager::Cmd_ThermalLog> },
    };

    // Persisted so an unattended multi-hour run survives a reboot. Custom is
    // never restored (its lever positions aren't stored) — it falls back to
    // Baseline. Keys stay within NVS's 15-character limit.
    inline static UInt32Setting modeSetting_   { "thermal.mode",   "Thermal mode (0-3)",   0 };
    inline static BoolSetting   cycleSetting_  { "thermal.cycle",  "Thermal cycle A/B",    false };
    inline static UInt32Setting cycleASetting_ { "thermal.cycleA", "Thermal cycle A mode", 0 };
    inline static UInt32Setting cycleBSetting_ { "thermal.cycleB", "Thermal cycle B mode", 1 };
    inline static UInt32Setting dwellSetting_  { "thermal.dwell",  "Thermal dwell (min)",  90 };
    inline static UInt32Setting sampleSetting_ { "thermal.sample", "Thermal sample (s)",   30 };
    inline static UInt32Setting floorSetting_  { "thermal.floor",  "Thermal floor (min)",  30 };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    ThermalMode mode_ = ThermalMode::Baseline;
    Levers      levers_{ 100, 0, FullCpuMhz };
    int         cpuMhzActual_ = FullCpuMhz;
    bool        cpuControl_ = false;   // false when the build has no CONFIG_PM_ENABLE

    bool        cycle_ = false;
    ThermalMode cycleA_ = ThermalMode::Baseline;
    ThermalMode cycleB_ = ThermalMode::DarkScreen;
    uint32_t    dwellMin_ = 90;
    uint32_t    sampleSec_ = 30;
    uint32_t    floorMin_ = 30;

    // Set by the command, acted on by the task: a floor run takes the radio down
    // with it, so the reply must be on the wire before it starts.
    bool floorRequested_ = false;

    int64_t stateEnteredUs_ = 0;
    int64_t lastSampleUs_ = 0;

    Sample samples_[MaxSamples];
    int    sampleCount_ = 0;
    int    sampleHead_ = 0;   // next slot to write
};
