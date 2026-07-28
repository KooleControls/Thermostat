#include "ThermalTestManager.h"
#include "CommandManager/CommandManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"
#include "OpenThermManager/OpenThermManager.h"
#include "NetworkManager/NetworkManager.h"
#include "SettingsManager/SettingsManager.h"
#include "Board.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

ThermalTestManager::ThermalTestManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void ThermalTestManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);
    serviceProvider_.getSettingsManager().Register({
        &modeSetting_, &cycleSetting_, &cycleASetting_, &cycleBSetting_,
        &dwellSetting_, &sampleSetting_, &floorSetting_,
    });

    // Only the four graded modes are restorable. Custom has no stored lever
    // positions, and Floor must never re-arm itself on boot — a mode that takes
    // the radio down is not something to wake up already committed to.
    auto restoreMode = [](uint32_t stored) {
        return stored <= static_cast<uint32_t>(ThermalMode::LowPower)
                   ? static_cast<ThermalMode>(stored)
                   : ThermalMode::Baseline;
    };
    cycle_ = cycleSetting_.Get();
    cycleA_ = restoreMode(cycleASetting_.Get());
    cycleB_ = restoreMode(cycleBSetting_.Get());
    dwellMin_ = dwellSetting_.Get();
    sampleSec_ = sampleSetting_.Get();
    floorMin_ = floorSetting_.Get();
    if (dwellMin_ < MinDwellMin) dwellMin_ = MinDwellMin;
    if (dwellMin_ > MaxDwellMin) dwellMin_ = MaxDwellMin;
    if (sampleSec_ < MinSampleSec) sampleSec_ = MinSampleSec;
    if (sampleSec_ > MaxSampleSec) sampleSec_ = MaxSampleSec;
    if (floorMin_ < MinFloorMin) floorMin_ = MinFloorMin;
    if (floorMin_ > MaxFloorMin) floorMin_ = MaxFloorMin;

    // Does this build let us move the CPU clock at all? Pinning max == min at
    // the default frequency is a no-op on a PM-enabled build and tells us
    // whether the lever exists; ESP_ERR_NOT_SUPPORTED means CONFIG_PM_ENABLE
    // is off and the UI should grey the lever out instead of lying about it.
    cpuControl_ = SetCpuMhz(FullCpuMhz);
    if (!cpuControl_)
        ESP_LOGW(TAG, "CPU frequency lever unavailable (build has no CONFIG_PM_ENABLE)");

    ApplyMode(cycle_ ? cycleA_ : restoreMode(modeSetting_.Get()));

    // One stable header line so a console capture is a complete CSV file.
    ESP_LOGI(TAG, "THERMAL,uptime_s,mode,backlight_pct,pclk_khz,cpu_mhz,room_c,rh_pct,die_c,state_s,ot_linked");

    lastSampleUs_ = 0;   // sample immediately on the first tick
    task_.Init("thermal", 4, 4096);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (mode %s, cycle %s, dwell %lu min, sample %lu s)",
             ModeName(mode_), cycle_ ? "on" : "off",
             (unsigned long)dwellMin_, (unsigned long)sampleSec_);
}

// ──────────────────────────────────────────────────────────────
// Modes and levers
// ──────────────────────────────────────────────────────────────

const char *ThermalTestManager::ModeName(ThermalMode mode)
{
    switch (mode)
    {
        case ThermalMode::Baseline:   return "baseline";
        case ThermalMode::DarkScreen: return "dark";
        case ThermalMode::PanelIdle:  return "panelidle";
        case ThermalMode::LowPower:   return "lowpower";
        case ThermalMode::Floor:      return "floor";
        case ThermalMode::Custom:     return "custom";
    }
    return "baseline";
}

bool ThermalTestManager::ParseMode(const char *name, ThermalMode &out)
{
    if (name == nullptr || name[0] == '\0') return false;
    for (uint8_t i = 0; i <= static_cast<uint8_t>(ThermalMode::Custom); ++i)
    {
        ThermalMode candidate = static_cast<ThermalMode>(i);
        if (std::strcmp(name, ModeName(candidate)) == 0)
        {
            out = candidate;
            return true;
        }
    }
    return false;
}

ThermalTestManager::Levers ThermalTestManager::LeversFor(ThermalMode mode)
{
    switch (mode)
    {
        case ThermalMode::Baseline:   return { 100, 0,          FullCpuMhz };
        case ThermalMode::DarkScreen: return { 0,   0,          FullCpuMhz };
        case ThermalMode::PanelIdle:  return { 0,   IdlePclkHz, FullCpuMhz };
        case ThermalMode::LowPower:   return { 0,   IdlePclkHz, LowPowerCpuMhz };
        case ThermalMode::Floor:      return { 0,   FloorPclkHz, LowPowerCpuMhz };
        case ThermalMode::Custom:     return { 0,   IdlePclkHz, FullCpuMhz };
    }
    return { 100, 0, FullCpuMhz };
}

void ThermalTestManager::ApplyMode(ThermalMode mode)
{
    ThermalMode previous = mode_;
    mode_ = mode;
    ApplyLevers(LeversFor(mode));
    // A new thermal state always starts its settle clock, cycling or not.
    stateEnteredUs_ = esp_timer_get_time();

    ESP_LOGI(TAG, "MODE %s -> %s (backlight %u%%, pclk %lu kHz, cpu %d MHz)",
             ModeName(previous), ModeName(mode_), levers_.backlight,
             (unsigned long)(serviceProvider_.getBoard().GetPanelPclk() / 1000),
             cpuMhzActual_);
}

void ThermalTestManager::ApplyLevers(const Levers &levers)
{
    Board &board = serviceProvider_.getBoard();

    board.SetBacklightPercent(levers.backlight);
    board.SetPanelPclk(levers.pclkHz != 0 ? levers.pclkHz : Board::PanelPclkDefaultHz);
    SetCpuMhz(levers.cpuMhz);

    levers_ = levers;

    // The dwell clock only restarts when nothing is cycling. Otherwise a lever
    // nudged every quarter of an hour would keep pushing the rotation out and
    // the cycle would never advance.
    if (!cycle_) stateEnteredUs_ = esp_timer_get_time();
}

bool ThermalTestManager::SetCpuMhz(int mhz)
{
    esp_pm_config_t cfg = {};
    cfg.max_freq_mhz = mhz;
    cfg.min_freq_mhz = mhz;   // pin it: dynamic scaling would blur the measurement
    cfg.light_sleep_enable = false;

    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK)
    {
        // Expected on a build without CONFIG_PM_ENABLE; anything else is worth a shout.
        if (err != ESP_ERR_NOT_SUPPORTED)
            ESP_LOGW(TAG, "esp_pm_configure(%d MHz) failed: %s", mhz, esp_err_to_name(err));
        return false;
    }
    cpuMhzActual_ = mhz;
    return true;
}

void ThermalTestManager::PersistConfig()
{
    modeSetting_.Set(static_cast<uint32_t>(mode_));
    cycleSetting_.Set(cycle_);
    cycleASetting_.Set(static_cast<uint32_t>(cycleA_));
    cycleBSetting_.Set(static_cast<uint32_t>(cycleB_));
    dwellSetting_.Set(dwellMin_);
    sampleSetting_.Set(sampleSec_);
    floorSetting_.Set(floorMin_);
    serviceProvider_.getSettingsManager().Save();
}

// ──────────────────────────────────────────────────────────────
// Sampling
// ──────────────────────────────────────────────────────────────

void ThermalTestManager::Loop()
{
    while (true)
    {
        int64_t now = esp_timer_get_time();

        bool startFloor = false;
        {
            LOCK(mutex_);
            startFloor = floorRequested_;
            floorRequested_ = false;
        }
        if (startFloor)
        {
            RunFloor();
            continue;   // the soak swallowed hours; re-read the clock
        }

        {
            LOCK(mutex_);
            if (cycle_ && (now - stateEnteredUs_) >= (int64_t)dwellMin_ * 60 * 1000000LL)
                ApplyMode(mode_ == cycleA_ ? cycleB_ : cycleA_);
        }

        bool due = false;
        {
            LOCK(mutex_);
            due = (now - lastSampleUs_) >= (int64_t)sampleSec_ * 1000000LL;
            if (due) lastSampleUs_ = now;
        }
        if (due) TakeSample();

        vTaskDelay(pdMS_TO_TICKS(TickMs));
    }
}

void ThermalTestManager::TakeSample(bool direct)
{
    // Read every source before taking the lock: these calls reach into other
    // managers and the I2C cache, and none of them needs our state.
    Sample s;
    s.uptimeS = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    if (direct)
    {
        // Sleep soak: the sampling task is the only thing running often enough
        // to reach the sensor, and the cache would always look stale.
        s.roomValid = serviceProvider_.getRoomTemperatureManager().SampleNow(s.room, s.humidity);
        s.humidityValid = s.roomValid;
    }
    else
    {
        s.roomValid = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(s.room);
        s.humidityValid = serviceProvider_.getRoomTemperatureManager().GetRoomHumidity(s.humidity);
    }
    s.dieValid = serviceProvider_.getBoard().GetSocTemperature().ReadCelsius(s.die);

    {
        LOCK(mutex_);
        s.mode = static_cast<uint8_t>(mode_);
        s.backlight = levers_.backlight;

        samples_[sampleHead_] = s;
        sampleHead_ = (sampleHead_ + 1) % MaxSamples;
        if (sampleCount_ < MaxSamples) sampleCount_++;
    }

    LogSample(s);
}

void ThermalTestManager::LogSample(const Sample &s)
{
    // Blank fields rather than sentinel values: the line is meant to be read
    // straight into a spreadsheet or a dataframe.
    char room[12] = {};
    char rh[12] = {};
    char die[12] = {};
    if (s.roomValid)     snprintf(room, sizeof(room), "%.2f", s.room);
    if (s.humidityValid) snprintf(rh, sizeof(rh), "%.1f", s.humidity);
    if (s.dieValid)      snprintf(die, sizeof(die), "%.1f", s.die);

    uint32_t stateS;
    {
        LOCK(mutex_);
        stateS = static_cast<uint32_t>((esp_timer_get_time() - stateEnteredUs_) / 1000000);
    }

    ESP_LOGI(TAG, "THERMAL,%lu,%s,%u,%lu,%d,%s,%s,%s,%lu,%d",
             (unsigned long)s.uptimeS, ModeName(static_cast<ThermalMode>(s.mode)),
             s.backlight,
             (unsigned long)(serviceProvider_.getBoard().GetPanelPclk() / 1000),
             cpuMhzActual_, room, rh, die, (unsigned long)stateS,
             serviceProvider_.getOpenThermManager().GetState().linked ? 1 : 0);
}

void ThermalTestManager::RunFloor()
{
    uint32_t minutes;
    uint32_t sampleSec;
    {
        LOCK(mutex_);
        minutes = floorMin_;
        sampleSec = sampleSec_;
        mode_ = ThermalMode::Floor;
        stateEnteredUs_ = esp_timer_get_time();
    }

    float room = 0, rh = 0, die = 0;
    bool haveStart = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(room);
    serviceProvider_.getBoard().GetSocTemperature().ReadCelsius(die);

    ESP_LOGW(TAG, "FLOOR start: %lu min, sampling every %lu s. Radio off, panel in "
                  "reset, CPU duty-cycled. Room %.2f C, die %.1f C at entry.",
             (unsigned long)minutes, (unsigned long)sampleSec,
             haveStart ? room : 0.0f, die);
    ESP_LOGW(TAG, "FLOOR: the display will NOT come back without a reboot, and the "
                  "OpenTherm master stops being serviced for the duration.");

    Board &board = serviceProvider_.getBoard();
    board.SetBacklightPercent(0);
    board.SetPanelPclk(FloorPclkHz);
    board.HoldPanelInReset();
    SetCpuMhz(LowPowerCpuMhz);

    // Last thing before going dark: everything above still needed a working log
    // path, and this takes the network away.
    serviceProvider_.getNetworkManager().StopRadio();

    int64_t endUs = esp_timer_get_time() + (int64_t)minutes * 60 * 1000000LL;
    uint32_t slept = 0;
    uint32_t rejected = 0;
    while (esp_timer_get_time() < endUs)
    {
        esp_sleep_enable_timer_wakeup((uint64_t)sampleSec * 1000000ULL);
        esp_err_t err = esp_light_sleep_start();
        if (err == ESP_OK)
        {
            slept++;
        }
        else
        {
            // A rejected sleep still has to pass the time, or this becomes a
            // busy loop that heats the very thing we are measuring.
            rejected++;
            vTaskDelay(pdMS_TO_TICKS(sampleSec * 1000));
        }
        TakeSample(true);
    }

    serviceProvider_.getNetworkManager().RestartRadio();
    SetCpuMhz(FullCpuMhz);

    bool haveEnd = serviceProvider_.getRoomTemperatureManager().SampleNow(room, rh);
    serviceProvider_.getBoard().GetSocTemperature().ReadCelsius(die);
    ESP_LOGW(TAG, "FLOOR done after %lu min (%lu sleeps, %lu rejected). "
                  "Room %.2f C, RH %.1f %%, die %.1f C. Radio back; reboot for the display.",
             (unsigned long)minutes, (unsigned long)slept, (unsigned long)rejected,
             haveEnd ? room : 0.0f, rh, die);

    // Land somewhere honest: the panel is dead, so Dark is what the hardware is
    // actually doing, and a persisted Floor must never re-arm itself on boot.
    {
        LOCK(mutex_);
        mode_ = ThermalMode::DarkScreen;
        stateEnteredUs_ = esp_timer_get_time();
    }
    PersistConfig();
}

bool ThermalTestManager::DeltaOver(uint32_t windowS, float Sample::*value,
                                   bool Sample::*valid, float &out) const
{
    if (sampleCount_ < 2) return false;

    int newestIndex = (sampleHead_ - 1 + MaxSamples) % MaxSamples;
    const Sample &newest = samples_[newestIndex];
    if (!(newest.*valid)) return false;

    // Walk back from the newest to the oldest sample still inside the window;
    // whatever we end on is the reference. A buffer shorter than the window
    // simply gives the change over as much history as we have.
    const Sample *reference = nullptr;
    for (int i = 1; i < sampleCount_; ++i)
    {
        const Sample &candidate = samples_[(newestIndex - i + MaxSamples) % MaxSamples];
        if (!(candidate.*valid)) continue;
        reference = &candidate;
        if (newest.uptimeS - candidate.uptimeS >= windowS) break;
    }
    if (reference == nullptr) return false;

    out = newest.*value - reference->*value;
    return true;
}

// ──────────────────────────────────────────────────────────────
// WebSocket commands
// ──────────────────────────────────────────────────────────────

void ThermalTestManager::WriteStatus(Stream &out)
{
    LOCK(mutex_);

    JsonObject resp(out);
    resp.field("mode", ModeName(mode_));
    resp.field("backlight", static_cast<int32_t>(levers_.backlight));
    resp.field("pclkHz", static_cast<uint32_t>(serviceProvider_.getBoard().GetPanelPclk()));
    resp.field("pclkDefaultHz", static_cast<uint32_t>(Board::PanelPclkDefaultHz));
    resp.field("cpuMhz", static_cast<int32_t>(cpuMhzActual_));
    resp.field("cpuControl", cpuControl_);

    resp.field("cycle", cycle_);
    resp.field("cycleA", ModeName(cycleA_));
    resp.field("cycleB", ModeName(cycleB_));
    resp.field("dwellMin", static_cast<uint32_t>(dwellMin_));
    resp.field("sampleSec", static_cast<uint32_t>(sampleSec_));
    resp.field("floorMin", static_cast<uint32_t>(floorMin_));
    resp.field("panelDead", serviceProvider_.getBoard().IsPanelInReset());
    resp.field("secondsInState",
               static_cast<uint32_t>((esp_timer_get_time() - stateEnteredUs_) / 1000000));

    float room = 0, rh = 0, die = 0;
    bool roomValid = false, rhValid = false, dieValid = false;
    if (sampleCount_ > 0)
    {
        const Sample &newest = samples_[(sampleHead_ - 1 + MaxSamples) % MaxSamples];
        room = newest.room;         roomValid = newest.roomValid;
        rh = newest.humidity;       rhValid = newest.humidityValid;
        die = newest.die;           dieValid = newest.dieValid;
    }
    resp.field("room", room);
    resp.field("roomValid", roomValid);
    resp.field("humidity", rh);
    resp.field("humidityValid", rhValid);
    resp.field("die", die);
    resp.field("dieValid", dieValid);

    // How far the two temperatures have moved over the last half hour — the
    // cheap read on "has this state settled yet".
    float delta = 0;
    if (DeltaOver(DeltaWindowS, &Sample::room, &Sample::roomValid, delta))
        resp.field("roomDelta", delta);
    if (DeltaOver(DeltaWindowS, &Sample::die, &Sample::dieValid, delta))
        resp.field("dieDelta", delta);
    resp.field("deltaWindowS", static_cast<uint32_t>(DeltaWindowS));

    resp.field("samples", static_cast<int32_t>(sampleCount_));
    resp.field("otLinked", serviceProvider_.getOpenThermManager().GetState().linked);
}

void ThermalTestManager::Cmd_ThermalStatus(Stream &, Stream &out)
{
    WriteStatus(out);
}

void ThermalTestManager::Cmd_ThermalSet(Stream &in, Stream &out)
{
    JsonReader<256> json(in);

    char        name[16] = {};
    ThermalMode parsed = ThermalMode::Baseline;
    bool        haveMode = json.GetString("mode", name, sizeof(name)) && ParseMode(name, parsed);

    char        aName[16] = {};
    ThermalMode cycleA = cycleA_;
    bool        haveA = json.GetString("cycleA", aName, sizeof(aName)) && ParseMode(aName, cycleA);
    char        bName[16] = {};
    ThermalMode cycleB = cycleB_;
    bool        haveB = json.GetString("cycleB", bName, sizeof(bName)) && ParseMode(bName, cycleB);

    int32_t cycle = json.GetInt("cycle", -1);
    int32_t dwell = json.GetInt("dwellMin", -1);
    int32_t sample = json.GetInt("sampleSec", -1);
    int32_t floorMin = json.GetInt("floorMin", -1);

    // Individual lever fields are the escape hatch; using any of them puts the
    // rig in Custom, because the state no longer matches a named mode.
    int32_t backlight = json.GetInt("backlight", -1);
    int32_t pclkHz = json.GetInt("pclkHz", -1);
    int32_t cpuMhz = json.GetInt("cpuMhz", -1);
    bool    haveLever = backlight >= 0 || pclkHz > 0 || cpuMhz > 0;

    {
        LOCK(mutex_);

        if (haveA) cycleA_ = cycleA;
        if (haveB) cycleB_ = cycleB;
        if (dwell >= (int32_t)MinDwellMin && dwell <= (int32_t)MaxDwellMin)
            dwellMin_ = static_cast<uint32_t>(dwell);
        if (sample >= (int32_t)MinSampleSec && sample <= (int32_t)MaxSampleSec)
            sampleSec_ = static_cast<uint32_t>(sample);
        if (floorMin >= (int32_t)MinFloorMin && floorMin <= (int32_t)MaxFloorMin)
            floorMin_ = static_cast<uint32_t>(floorMin);

        if (cycle >= 0)
        {
            bool wanted = (cycle != 0);
            if (wanted != cycle_)
            {
                cycle_ = wanted;
                // Starting a cycle always begins at A, so the first dwell is a
                // full one and the run is reproducible.
                if (cycle_ && !haveMode) ApplyMode(cycleA_);
            }
        }

        if (haveMode && parsed == ThermalMode::Floor)
        {
            // Handed to the task rather than done here: the soak stops the radio,
            // so this reply has to be on the wire first.
            floorRequested_ = true;
            cycle_ = false;
            ESP_LOGW(TAG, "Floor run requested (%lu min) — the web UI will drop",
                     (unsigned long)floorMin_);
        }
        else if (haveMode)
        {
            ApplyMode(parsed);
            // A hand-picked mode during a cycle would be rotated away at the
            // next dwell; treat it as taking manual control instead.
            if (cycle_ && cycle < 0) cycle_ = false;
        }

        if (haveLever)
        {
            Levers levers = levers_;
            if (backlight >= 0) levers.backlight = static_cast<uint8_t>(backlight > 100 ? 100 : backlight);
            if (pclkHz > 0) levers.pclkHz = static_cast<uint32_t>(pclkHz);
            if (cpuMhz > 0) levers.cpuMhz = cpuMhz;
            // Custom, but a running cycle keeps running: a lever nudged
            // mid-run (a backlight wake pulse, say) must not silently end an
            // overnight measurement. The next dwell rotation puts a named mode
            // back. Only picking a mode by hand takes manual control.
            mode_ = ThermalMode::Custom;
            ApplyLevers(levers);
            ESP_LOGI(TAG, "MODE custom (backlight %u%%, pclk %lu kHz, cpu %d MHz)",
                     levers_.backlight,
                     (unsigned long)(serviceProvider_.getBoard().GetPanelPclk() / 1000),
                     cpuMhzActual_);
        }
    }

    // Outside the lock: an NVS commit is slow, and the sampling task should not
    // wait behind it.
    PersistConfig();

    WriteStatus(out);
}

void ThermalTestManager::Cmd_ThermalLog(Stream &, Stream &out)
{
    LOCK(mutex_);

    JsonObject resp(out);
    resp.field("deltaWindowS", static_cast<uint32_t>(DeltaWindowS));
    JsonArray rows = resp.array("samples");

    // Oldest first: the natural order for plotting and for appending to a file.
    int oldest = (sampleHead_ - sampleCount_ + MaxSamples) % MaxSamples;
    for (int i = 0; i < sampleCount_; ++i)
    {
        const Sample &s = samples_[(oldest + i) % MaxSamples];
        JsonObject row = rows.object();
        row.field("t", static_cast<uint32_t>(s.uptimeS));
        row.field("mode", ModeName(static_cast<ThermalMode>(s.mode)));
        row.field("backlight", static_cast<int32_t>(s.backlight));
        if (s.roomValid) row.field("room", s.room);
        if (s.humidityValid) row.field("humidity", s.humidity);
        if (s.dieValid) row.field("die", s.die);
    }
}
