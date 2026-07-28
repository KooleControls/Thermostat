#include "ThermalTestManager.h"
#include "CommandManager/CommandManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"
#include "OpenThermManager/OpenThermManager.h"
#include "NetworkManager/NetworkManager.h"
#include "Board.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "esp_log.h"
#include "esp_pm.h"
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

    // Does this build let us move the CPU clock at all? Pinning max == min at
    // the default frequency is a no-op on a PM-enabled build and tells us
    // whether the lever exists; ESP_ERR_NOT_SUPPORTED means CONFIG_PM_ENABLE is
    // off and the UI should grey it out instead of lying about it.
    cpuControl_ = SetCpuMhz(FullCpuMhz);
    if (!cpuControl_)
        ESP_LOGW(TAG, "CPU frequency lever unavailable (build has no CONFIG_PM_ENABLE)");

    ApplyMode(ThermalMode::Baseline);

    // One stable header so a console capture is a complete CSV file. The real
    // record is the gateway's log; this is for sanity-checking on the bench.
    ESP_LOGI(TAG, "THERMAL,uptime_s,mode,backlight_pct,pclk_khz,cpu_mhz,room_c,rh_pct,ot_linked");

    task_.Init("thermal", 4, 4096);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (mode %s)", ModeName(mode_));
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
        case ThermalMode::PanelOff:   return "paneloff";
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
        case ThermalMode::Baseline:   return { 100, 0,          FullCpuMhz, false };
        case ThermalMode::DarkScreen: return { 0,   0,          FullCpuMhz, false };
        case ThermalMode::PanelIdle:  return { 0,   IdlePclkHz, FullCpuMhz, false };
        case ThermalMode::PanelOff:   return { 0,   OffPclkHz,  LowCpuMhz,  true };
        case ThermalMode::Custom:     return { 0,   0,          FullCpuMhz, false };
    }
    return { 100, 0, FullCpuMhz, false };
}

void ThermalTestManager::ApplyMode(ThermalMode mode)
{
    ThermalMode previous = mode_;
    mode_ = mode;
    ApplyLevers(LeversFor(mode));
    stateEnteredUs_ = esp_timer_get_time();

    ESP_LOGI(TAG, "MODE %s -> %s (backlight %u%%, pclk %lu kHz, cpu %d MHz%s)",
             ModeName(previous), ModeName(mode_), levers_.backlight,
             (unsigned long)(serviceProvider_.getBoard().GetPanelPclk() / 1000),
             cpuMhzActual_,
             serviceProvider_.getBoard().IsPanelInReset() ? ", panel in reset" : "");
}

void ThermalTestManager::ApplyLevers(const Levers &levers)
{
    Board &board = serviceProvider_.getBoard();

    board.SetBacklightPercent(levers.backlight);
    board.SetPanelPclk(levers.pclkHz != 0 ? levers.pclkHz : Board::PanelPclkDefaultHz);
    SetCpuMhz(levers.cpuMhz);
    if (levers.panelReset) board.HoldPanelInReset();   // one-way; never undone here

    levers_ = levers;
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

// ──────────────────────────────────────────────────────────────
// Console trace
// ──────────────────────────────────────────────────────────────

void ThermalTestManager::Loop()
{
    // Ticks once a second so a deferred radio stop happens promptly; the console
    // trace is every SampleSec-th tick.
    int tick = 0;
    while (true)
    {
        bool stopRadio = false;
        {
            LOCK(mutex_);
            stopRadio = stopRadioRequested_;
            stopRadioRequested_ = false;
        }
        if (stopRadio) serviceProvider_.getNetworkManager().StopRadio();

        if (tick % SampleSec == 0) LogSample();
        tick++;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void ThermalTestManager::LogSample()
{
    // Die temperature is deliberately absent: it is a readout on the web UI and
    // nothing more — not a data series, not an input to anything.
    float room = 0, rh = 0;
    bool haveRoom = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(room);
    bool haveRh = serviceProvider_.getRoomTemperatureManager().GetRoomHumidity(rh);

    // Blank fields rather than sentinel values, so the line drops straight into
    // a spreadsheet next to the gateway's own log.
    char roomStr[12] = {};
    char rhStr[12] = {};
    if (haveRoom) snprintf(roomStr, sizeof(roomStr), "%.2f", room);
    if (haveRh)   snprintf(rhStr, sizeof(rhStr), "%.1f", rh);

    ThermalMode mode;
    uint8_t backlight;
    { LOCK(mutex_); mode = mode_; backlight = levers_.backlight; }

    ESP_LOGI(TAG, "THERMAL,%lu,%s,%u,%lu,%d,%s,%s,%d",
             (unsigned long)(esp_timer_get_time() / 1000000),
             ModeName(mode), backlight,
             (unsigned long)(serviceProvider_.getBoard().GetPanelPclk() / 1000),
             cpuMhzActual_, roomStr, rhStr,
             serviceProvider_.getOpenThermManager().GetState().linked ? 1 : 0);
}

// ──────────────────────────────────────────────────────────────
// WebSocket commands
// ──────────────────────────────────────────────────────────────

void ThermalTestManager::WriteStatus(Stream &out)
{
    float room = 0, rh = 0, die = 0;
    bool haveRoom = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(room);
    bool haveRh = serviceProvider_.getRoomTemperatureManager().GetRoomHumidity(rh);
    bool haveDie = serviceProvider_.getBoard().GetSocTemperature().ReadCelsius(die);

    LOCK(mutex_);

    JsonObject resp(out);
    resp.field("mode", ModeName(mode_));
    resp.field("backlight", static_cast<int32_t>(levers_.backlight));
    resp.field("pclkHz", static_cast<uint32_t>(serviceProvider_.getBoard().GetPanelPclk()));
    resp.field("cpuMhz", static_cast<int32_t>(cpuMhzActual_));
    resp.field("cpuControl", cpuControl_);
    resp.field("panelDead", serviceProvider_.getBoard().IsPanelInReset());
    resp.field("radioStopped", serviceProvider_.getNetworkManager().IsRadioStopped());
    resp.field("secondsInState",
               static_cast<uint32_t>((esp_timer_get_time() - stateEnteredUs_) / 1000000));

    resp.field("room", room);
    resp.field("roomValid", haveRoom);
    resp.field("humidity", rh);
    resp.field("humidityValid", haveRh);
    resp.field("die", die);
    resp.field("dieValid", haveDie);
    resp.field("otLinked", serviceProvider_.getOpenThermManager().GetState().linked);
}

void ThermalTestManager::Cmd_ThermalStatus(Stream &, Stream &out)
{
    WriteStatus(out);
}

void ThermalTestManager::Cmd_ThermalSet(Stream &in, Stream &out)
{
    JsonReader<128> json(in);

    char        name[16] = {};
    ThermalMode parsed = ThermalMode::Baseline;
    bool        haveMode = json.GetString("mode", name, sizeof(name)) && ParseMode(name, parsed);
    int32_t     backlight = json.GetInt("backlight", -1);
    int32_t     cpuMhz = json.GetInt("cpuMhz", 0);
    bool        stopRadio = json.GetBool("stopRadio", false);

    // The two ESP-side levers are independent of the panel ladder: the vendor
    // points out the SoC heats too, and attributing that needs the CPU clock and
    // the radio movable without also killing the display.
    if (cpuMhz == 80 || cpuMhz == 160 || cpuMhz == 240)
    {
        LOCK(mutex_);
        Levers levers = levers_;
        levers.cpuMhz = cpuMhz;
        mode_ = ThermalMode::Custom;
        ApplyLevers(levers);
        stateEnteredUs_ = esp_timer_get_time();
        ESP_LOGI(TAG, "MODE custom (cpu %d MHz)", cpuMhzActual_);
    }
    else if (stopRadio)
    {
        // Handed to the task rather than done here: stopping WiFi tears down the
        // socket this reply has to travel over, so the request would look like a
        // failure instead of a success.
        LOCK(mutex_);
        stopRadioRequested_ = true;
        ESP_LOGW(TAG, "Radio stop requested from the web UI");
    }
    else
    {
        LOCK(mutex_);
        if (haveMode)
        {
            ApplyMode(parsed);
        }
        else if (backlight >= 0)
        {
            // Brightness on its own: the state no longer matches a named mode.
            Levers levers = levers_;
            levers.backlight = static_cast<uint8_t>(backlight > 100 ? 100 : backlight);
            mode_ = ThermalMode::Custom;
            ApplyLevers(levers);
            stateEnteredUs_ = esp_timer_get_time();
            ESP_LOGI(TAG, "MODE custom (backlight %u%%)", levers_.backlight);
        }
    }

    WriteStatus(out);
}
