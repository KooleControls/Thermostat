# Climate PID Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `ClimateManager` that owns mode + setpoint, runs a deadband heating PID producing OpenTherm `t_set`, issues on/off cooling, adopts the ID 9 remote override, and degrades safely on sensor fault — while `OpenThermManager` slims to a pure transport.

**Architecture:** A header-only `PidController` (pure logic) reused by `ClimateManager` (a standard Strux manager with a 5 s control loop). ClimateManager reads room temp from `RoomTemperatureManager` and boiler state from `OpenThermManager`, computes an `OtDemand` heating slice, and pushes it through a new `SetHeatingDemand` slice setter. OpenThermManager stops mutating its own demand for overrides — it just exposes the raw ID 9 value.

**Tech Stack:** ESP-IDF v6.0, C++17, FreeRTOS via `lib/rtos` (`Task`, `Mutex`, `LOCK`, `InitState`), TypedSettings (NVS), `lib/json` (`JsonReader`/`JsonScope`), CommandManager `CommandEntry` tables.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-07-climate-pid-design.md` governs.
- PID constants (DIYLESS `diyless-thermostat-3.yaml`, reused algorithm/constants — **ESPHome is GPLv3, never its source**): kp **0.77**, ki **0.0005** per second, kd **0**, deadband **±0.5 °C**, in-band ki multiplier **0.15**, output averaging **10** samples outside band / **15** inside.
- Setpoint range **5–30 °C** (0.5° resolution at the UI; the manager clamps to [5,30]); frost setpoint **5.0 °C**; mode `Off=0 / Heat=1 / Cool=2`.
- Control loop cadence **5000 ms**; PID integral scaled by actual elapsed dt (seconds) via `esp_timer_get_time()`.
- `t_set` mapping: `output ≤ 0.01 → 0`; else `clamp(lo + output·(hi−lo), lo, hi)` with `lo=maxTSetLower` (default 30), `hi=maxTSetUpper` (default 80).
- Managers: `ServiceProvider&` ctor, copy/move deleted, `InitState`-guarded `Init()`; sources listed explicitly in `main/CMakeLists.txt`; `snprintf` bounds, no `strcpy`/`strcat`.
- No automated tests in this repo; each task's cycle is a green build (`idf.py build`). Hardware verification is Task 4.
- Build (PowerShell): `. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py build` from repo root. Never export.ps1. Full build takes minutes — timeout 600000 ms.

---

### Task 1: PidController + ClimateMode (pure headers)

**Files:**
- Create: `main/Application/ClimateManager/PidController.h`
- Create: `main/Application/ClimateManager/ClimateMode.h`

**Interfaces:**
- Consumes: nothing (pure logic, `<cmath>`/`<cstring>` only).
- Produces: `PidController::Update(float setpoint, float measured, float dtSeconds) -> float` (returns averaged output in [0,1]); `PidController::Reset()`; `enum class ClimateMode { Off=0, Heat=1, Cool=2 }`; `ClimateModeName(ClimateMode) -> const char*`; `ParseClimateMode(const char*, ClimateMode&) -> bool`. Task 3 relies on all of these, exactly these names.

- [ ] **Step 1: Create PidController.h**

`main/Application/ClimateManager/PidController.h`:

```cpp
#pragma once
#include <cmath>

// Clean-room reimplementation of the ESPHome climate PID (deadband variant)
// as configured in diyless-thermostat-3.yaml. ESPHome is GPLv3 — the algorithm
// and constants are reused, the source is not. Heat-only: output is a demand
// fraction clamped to [0,1]. kd is 0, so there is no derivative term at all.
//
// error = setpoint - measured (positive = too cold = wants heat). Inside the
// ±deadband the integral gain is cut (anti-windup near target) and the output
// is averaged over a longer window to stop hunting; outside, a shorter window.
class PidController
{
public:
    static constexpr float Kp = 0.77f;
    static constexpr float Ki = 0.0005f;      // per second
    static constexpr float Deadband = 0.5f;   // ±°C around setpoint
    static constexpr float DeadbandKiMul = 0.15f;
    static constexpr int   AvgOutside = 10;
    static constexpr int   AvgInside  = 15;
    static constexpr int   RingSize   = 15;

    float Update(float setpoint, float measured, float dtSeconds)
    {
        float error = setpoint - measured;
        bool  inBand = fabsf(error) <= Deadband;
        float ki = inBand ? Ki * DeadbandKiMul : Ki;

        integral_ += error * dtSeconds;
        // Clamp the integral *term* (ki·integral) to [0,1] so windup cannot
        // accumulate while the boiler saturates; back-solve integral_ to the
        // boundary using the current ki.
        float iTerm = ki * integral_;
        if (iTerm > 1.0f)      { iTerm = 1.0f; integral_ = 1.0f / ki; }
        else if (iTerm < 0.0f) { iTerm = 0.0f; integral_ = 0.0f; }

        float raw = Kp * error + iTerm;   // kd == 0
        if (raw < 0.0f) raw = 0.0f;
        if (raw > 1.0f) raw = 1.0f;

        ring_[head_] = raw;
        head_ = (head_ + 1) % RingSize;
        if (count_ < RingSize) count_++;

        int window = inBand ? AvgInside : AvgOutside;
        if (window > count_) window = count_;
        float sum = 0.0f;
        for (int i = 0; i < window; i++)
            sum += ring_[(head_ - 1 - i + RingSize) % RingSize];
        return sum / window;
    }

    void Reset()
    {
        integral_ = 0.0f;
        head_ = 0;
        count_ = 0;
    }

private:
    float integral_ = 0.0f;
    float ring_[RingSize] = {};
    int   head_ = 0;
    int   count_ = 0;
};
```

- [ ] **Step 2: Create ClimateMode.h**

`main/Application/ClimateManager/ClimateMode.h`:

```cpp
#pragma once
#include <cstring>

enum class ClimateMode { Off = 0, Heat = 1, Cool = 2 };

inline const char *ClimateModeName(ClimateMode m)
{
    switch (m)
    {
    case ClimateMode::Heat: return "heat";
    case ClimateMode::Cool: return "cool";
    default:                return "off";
    }
}

// Accepts "off"/"heat"/"cool" or "0"/"1"/"2". Returns false if unrecognized
// (caller keeps the current mode).
inline bool ParseClimateMode(const char *s, ClimateMode &out)
{
    if (!s) return false;
    if (!strcmp(s, "off")  || !strcmp(s, "0")) { out = ClimateMode::Off;  return true; }
    if (!strcmp(s, "heat") || !strcmp(s, "1")) { out = ClimateMode::Heat; return true; }
    if (!strcmp(s, "cool") || !strcmp(s, "2")) { out = ClimateMode::Cool; return true; }
    return false;
}
```

- [ ] **Step 3: Review the PID numerics against worked examples**

These are hand-verification anchors (no test harness in this repo). Trace `Update` from a fresh `PidController` (count_=0):

1. `Update(26, 24, 5)`: error 2 (outside band), integral 10, iTerm 0.0005·10=0.005, raw=0.77·2+0.005=1.545→clamp 1.0; window min(10,1)=1 → returns **1.0**.
2. Fresh, `Update(24, 23.7, 5)`: error 0.3 (inside band), ki 0.000075, integral 1.5, iTerm≈0.0001125, raw≈0.77·0.3=0.231; returns ≈**0.231**.
3. Fresh, `Update(24, 25, 5)`: error −1 (outside band), integral −5, iTerm −0.0025→clamp 0 (integral_→0), raw=0.77·(−1)=−0.77→clamp 0; returns **0.0**.

Confirm the code produces these. No commit yet if any disagree — fix the code first.

- [ ] **Step 4: Build**

These headers are compiled once Task 3 includes them; there is nothing to link yet. Verify they are self-contained by compiling a throwaway translation unit — run from repo root:

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; & "$env:IDF_PYTHON_ENV_PATH\..\..\..\tools\*\xtensa-esp-elf\*\bin\xtensa-esp32s3-elf-g++.exe" --version
```

If that compiler probe is awkward, skip it: the headers are trivially self-contained (only `<cmath>`/`<cstring>`), and Task 3's build is the real compile gate. Do NOT add a test framework or a scratch main to the repo.

- [ ] **Step 5: Commit**

```bash
git add main/Application/ClimateManager/PidController.h main/Application/ClimateManager/ClimateMode.h
git commit -m "Add PidController (deadband PID) and ClimateMode"
```

---

### Task 2: OpenThermManager — slice setters, override exposure, retire otSet heating writes

**Files:**
- Modify: `main/Application/OpenThermManager/OpenThermManager.h`
- Modify: `main/Application/OpenThermManager/OpenThermManager.cpp`

**Interfaces:**
- Consumes: existing `OtDemand`, `OtBoilerState`, `Read()`, `LOCK(mutex_)`.
- Produces: `OpenThermManager::SetHeatingDemand(bool chEnable, bool coolEnable, float roomSetpoint, float tSet)`; `OpenThermManager::SetDhwDemand(bool dhwEnable, float dhwSetpoint)`; `OtBoilerState::overrideSetpoint` (float, 0 = none). Task 3 relies on `SetHeatingDemand` and `overrideSetpoint`.

- [ ] **Step 1: Add overrideSetpoint to OtBoilerState**

In `main/Application/OpenThermManager/OpenThermManager.h`, inside `struct OtBoilerState`, after the `maxTSetUpper`/`maxTSetLower` lines:

```cpp
    // Remote setpoint override from the boiler/gateway (ID 9). 0 = none.
    // Exposed raw; the setpoint owner (ClimateManager) decides adoption.
    float overrideSetpoint = 0;
```

- [ ] **Step 2: Replace SetDemand declaration with the two slice setters**

In the same header's `public:` section, replace:

```cpp
    void          SetDemand(const OtDemand &d);
```

with:

```cpp
    // Demand is written in two independent slices so distinct owners don't
    // clobber each other: ClimateManager owns heating/cooling; the DHW owner
    // (otSet today, hot-water manager in item 6) owns DHW.
    void SetHeatingDemand(bool chEnable, bool coolEnable, float roomSetpoint, float tSet);
    void SetDhwDemand(bool dhwEnable, float dhwSetpoint);
```

- [ ] **Step 3: Remove the AdoptOverride declaration**

In the header's `private:` section, delete this line:

```cpp
    void     AdoptOverride(float setpoint);
```

- [ ] **Step 4: Replace the SetDemand implementation with the two setters**

In `main/Application/OpenThermManager/OpenThermManager.cpp`, replace the whole `SetDemand` function (the `void OpenThermManager::SetDemand(const OtDemand &d) { ... }` block) with:

```cpp
void OpenThermManager::SetHeatingDemand(bool chEnable, bool coolEnable, float roomSetpoint, float tSet)
{
    LOCK(mutex_);
    bool changed = demand_.chEnable != chEnable ||
                   demand_.coolEnable != coolEnable ||
                   fabsf(demand_.roomSetpoint - roomSetpoint) > 0.01f ||
                   fabsf(demand_.tSet - tSet) > 0.01f;
    if (changed)
    {
        demand_.chEnable   = chEnable;
        demand_.coolEnable = coolEnable;
        demand_.roomSetpoint = roomSetpoint;
        demand_.tSet       = tSet;
        demandDirty_ = true;
    }
}

void OpenThermManager::SetDhwDemand(bool dhwEnable, float dhwSetpoint)
{
    LOCK(mutex_);
    bool changed = demand_.dhwEnable != dhwEnable ||
                   fabsf(demand_.dhwSetpoint - dhwSetpoint) > 0.01f;
    if (changed)
    {
        demand_.dhwEnable   = dhwEnable;
        demand_.dhwSetpoint = dhwSetpoint;
        demandDirty_ = true;
    }
}
```

- [ ] **Step 5: Store the ID 9 override into state instead of adopting it**

In `OpenThermManager.cpp`, replace the whole `DoOverrideRead` function with:

```cpp
void OpenThermManager::DoOverrideRead(OtLink &link)
{
    uint16_t raw = 0;
    if (Read(link, ID_TROVRD, raw) != OtResult::Ok)
        return;

    float ovr = OtFrame::FromF88(raw);
    if (ovr < 0.0f) ovr = 0.0f;               // negative is meaningless
    if (ovr > 0.0f)                            // clamp a real override; 0 = none
    {
        if (ovr < 5.0f) ovr = 5.0f;
        else if (ovr > 30.0f) ovr = 30.0f;
    }
    LOCK(mutex_);
    state_.overrideSetpoint = ovr;             // ClimateManager adopts it
}
```

Then delete the entire `AdoptOverride` function definition (`void OpenThermManager::AdoptOverride(float setpoint) { ... }`).

- [ ] **Step 6: Expose overrideSetpoint in otStatus**

In `OpenThermManager.cpp`, in `Cmd_Status`, after the `resp.field("maxTSetLower", s.maxTSetLower);` line add:

```cpp
    resp.field("overrideSetpoint", s.overrideSetpoint);
```

- [ ] **Step 7: Retire otSet's heating writes; keep DHW via SetDhwDemand**

In `OpenThermManager.cpp`, replace the whole `Cmd_Set` function with:

```cpp
void OpenThermManager::Cmd_Set(Stream &in, Stream &out)
{
    // Heating/cooling demand is ClimateManager's now (climateSet). otSet only
    // pokes DHW until the hot-water manager (item 6) takes it over.
    JsonReader<256> json(in);
    OtDemand d = GetDemand();
    bool  dhw    = d.dhwEnable;
    float dhwSet = d.dhwSetpoint;

    int i = json.GetInt("dhw", -1);
    if (i >= 0) dhw = (i != 0);
    float f = json.GetFloat("dhwSetpoint", NAN);
    if (!std::isnan(f))
    {
        if (f < 30.0f) f = 30.0f;
        if (f > 80.0f) f = 80.0f;
        dhwSet = f;
    }
    SetDhwDemand(dhw, dhwSet);
    Cmd_Status(in, out);   // reply with the resulting full state
}
```

- [ ] **Step 8: Remove the now-unused clamp helpers**

The `ClampedAssign` and `AssignFlag` free functions in the anonymous namespace at the top of `OpenThermManager.cpp` were only used by the old `Cmd_Set`. Delete both function definitions (leave the data-ID constants, `Slot`, `kRotation`, and `static_assert` intact). Confirm no other references remain: search the file for `ClampedAssign` and `AssignFlag` — there should be none after deletion.

- [ ] **Step 9: Build**

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py build
```
Expected: `Project build complete`. (`SetHeatingDemand` has no caller yet — that is fine; Task 3 adds it.)

- [ ] **Step 10: Commit**

```bash
git add main/Application/OpenThermManager/OpenThermManager.h main/Application/OpenThermManager/OpenThermManager.cpp
git commit -m "OpenThermManager: split demand into heating/DHW slices; expose ID 9 override raw; retire otSet heating writes"
```

---

### Task 3: ClimateManager + wiring

**Files:**
- Create: `main/Application/ClimateManager/ClimateManager.h`
- Create: `main/Application/ClimateManager/ClimateManager.cpp`
- Modify: `main/Application/ServiceProvider.h`
- Modify: `main/Application/ApplicationContext.h`
- Modify: `main/main.cpp`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `PidController`/`ClimateMode` (Task 1); `OpenThermManager::SetHeatingDemand`, `OpenThermManager::GetState()` → `OtBoilerState` with `overrideSetpoint`/`coolingSupported`/`maxTSetLower`/`maxTSetUpper` (Task 2); `RoomTemperatureManager::GetRoomTemperature(float&)` (existing); `SettingsManager::Register`/`Save`, `UInt32Setting`/`FloatSetting` (existing); `CommandManager::Register`.
- Produces: `ServiceProvider::getClimateManager() -> ClimateManager&`.

- [ ] **Step 1: Create ClimateManager.h**

`main/Application/ClimateManager/ClimateManager.h`:

```cpp
#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include "ClimateMode.h"
#include "PidController.h"
#include <cstdint>

// Owns the thermostat's control logic: mode (Off/Heat/Cool) + setpoint, a
// deadband heating PID producing OpenTherm t_set, on/off cooling demand, and
// remote-override (ID 9) adoption. Consumes RoomTemperatureManager (measured
// temp) and OpenThermManager (boiler caps + override in; heating demand out).
class ClimateManager
{
    static constexpr const char *TAG = "ClimateManager";
    static constexpr int   LoopDelayMs = 5000;
    static constexpr float kFrostSetpointC = 5.0f;
    static constexpr float kSetpointMin = 5.0f;
    static constexpr float kSetpointMax = 30.0f;

public:
    explicit ClimateManager(ServiceProvider &serviceProvider);

    ClimateManager(const ClimateManager &) = delete;
    ClimateManager &operator=(const ClimateManager &) = delete;
    ClimateManager(ClimateManager &&) = delete;
    ClimateManager &operator=(ClimateManager &&) = delete;

    void Init();

private:
    void Loop();
    void ControlStep();
    float OutputToTSet(float output, float loBound, float hiBound);
    void  PushSafeState();
    void  Cmd_ClimateSet(Stream &in, Stream &out);
    void  Cmd_ClimateStatus(Stream &in, Stream &out);
    void  WriteStatus(Stream &out);

    inline static CommandEntry commands_[] = {
        { "climateSet",    &InvokeCommand<&ClimateManager::Cmd_ClimateSet> },
        { "climateStatus", &InvokeCommand<&ClimateManager::Cmd_ClimateStatus> },
    };

    inline static UInt32Setting modeSetting_{ "climate.mode", "Climate Mode", (uint32_t)ClimateMode::Off };
    inline static FloatSetting  setpointSetting_{ "climate.setpoint", "Setpoint", 20.0f };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;
    PidController pid_;

    // state (guarded by mutex_)
    ClimateMode mode_ = ClimateMode::Off;
    float userSetpoint_ = 20.0f;
    float lastRoomTemp_ = 0.0f;
    bool  lastRoomValid_ = false;
    float lastActiveSetpoint_ = 0.0f;
    float lastPidOutput_ = 0.0f;
    float lastTSet_ = 0.0f;
    bool  lastChEnable_ = false;
    bool  lastCoolEnable_ = false;
    bool  lastOverrideActive_ = false;

    // loop-task-only
    int64_t lastStepUs_ = -1;
    bool    lastSafe_ = false;   // fault-log edge detector
};
```

- [ ] **Step 2: Create ClimateManager.cpp**

`main/Application/ClimateManager/ClimateManager.cpp`:

```cpp
#include "ClimateManager.h"
#include "CommandManager/CommandManager.h"
#include "OpenThermManager.h"
#include "RoomTemperatureManager.h"
#include "SettingsManager/SettingsManager.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

ClimateManager::ClimateManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void ClimateManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);
    serviceProvider_.getSettingsManager().Register({ &modeSetting_, &setpointSetting_ });

    {
        LOCK(mutex_);
        mode_ = (ClimateMode)modeSetting_.Get();
        userSetpoint_ = setpointSetting_.Get();
    }

    task_.Init("climate", 5, 8192);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (mode %s, setpoint %.1f)",
             ClimateModeName(mode_), userSetpoint_);
}

void ClimateManager::Loop()
{
    while (true)
    {
        ControlStep();
        vTaskDelay(pdMS_TO_TICKS(LoopDelayMs));
    }
}

void ClimateManager::ControlStep()
{
    int64_t now = esp_timer_get_time();
    float dt = (lastStepUs_ < 0) ? (LoopDelayMs / 1000.0f)
                                 : (now - lastStepUs_) / 1000000.0f;
    lastStepUs_ = now;

    float room = 0;
    bool  roomValid = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(room);
    OtBoilerState boiler = serviceProvider_.getOpenThermManager().GetState();

    ClimateMode mode;
    float userSp;
    { LOCK(mutex_); mode = mode_; userSp = userSetpoint_; }

    if (!roomValid)
    {
        if (!lastSafe_)
        {
            ESP_LOGW(TAG, "Room temp invalid - safe state (no demand)");
            lastSafe_ = true;
        }
        pid_.Reset();
        PushSafeState();
        { LOCK(mutex_); lastRoomValid_ = false; }
        return;
    }
    if (lastSafe_)
    {
        ESP_LOGI(TAG, "Room temp valid - resuming control");
        lastSafe_ = false;
    }

    bool  overrideActive = false;
    float activeSp;
    if (mode != ClimateMode::Off && boiler.overrideSetpoint > 0.0f)
    {
        activeSp = boiler.overrideSetpoint;
        overrideActive = true;
    }
    else if (mode == ClimateMode::Off)
    {
        activeSp = kFrostSetpointC;
    }
    else
    {
        activeSp = userSp;
    }

    if (overrideActive && !lastOverrideActive_)
        ESP_LOGI(TAG, "Remote override adopted: setpoint %.1f", activeSp);

    bool  ch = false, cool = false;
    float tset = 0.0f, output = 0.0f;

    if (mode == ClimateMode::Cool)
    {
        cool = boiler.coolingSupported && (room > activeSp + 0.5f);
        pid_.Reset();   // PID unused for cooling; keep it clean for next Heat
    }
    else   // Heat, or Off (frost) — both run the heating PID
    {
        output = pid_.Update(activeSp, room, dt);
        tset = OutputToTSet(output, boiler.maxTSetLower, boiler.maxTSetUpper);
        ch = (mode == ClimateMode::Heat) ? true : (tset > 0.0f);
    }

    serviceProvider_.getOpenThermManager().SetHeatingDemand(ch, cool, activeSp, tset);

    LOCK(mutex_);
    lastRoomTemp_ = room;
    lastRoomValid_ = true;
    lastActiveSetpoint_ = activeSp;
    lastPidOutput_ = output;
    lastTSet_ = tset;
    lastChEnable_ = ch;
    lastCoolEnable_ = cool;
    lastOverrideActive_ = overrideActive;
}

float ClimateManager::OutputToTSet(float output, float lo, float hi)
{
    if (lo <= 0.0f) lo = 30.0f;
    if (hi <= lo)   hi = 80.0f;
    if (output <= 0.01f) return 0.0f;
    float t = lo + output * (hi - lo);
    if (t < lo) t = lo;
    if (t > hi) t = hi;
    return t;
}

void ClimateManager::PushSafeState()
{
    serviceProvider_.getOpenThermManager().SetHeatingDemand(false, false, 0.0f, 0.0f);
    LOCK(mutex_);
    lastChEnable_ = false;
    lastCoolEnable_ = false;
    lastTSet_ = 0.0f;
    lastPidOutput_ = 0.0f;
    lastOverrideActive_ = false;
}

void ClimateManager::Cmd_ClimateSet(Stream &in, Stream &out)
{
    JsonReader<128> json(in);
    ClimateMode mode;
    float setpoint;
    bool changed = false;
    {
        LOCK(mutex_);
        char modeStr[8] = {};
        if (json.GetString("mode", modeStr, sizeof(modeStr)))
        {
            ClimateMode m;
            if (ParseClimateMode(modeStr, m) && m != mode_) { mode_ = m; changed = true; }
        }
        float sp = json.GetFloat("setpoint", NAN);
        if (!std::isnan(sp))
        {
            if (sp < kSetpointMin) sp = kSetpointMin;
            if (sp > kSetpointMax) sp = kSetpointMax;
            if (fabsf(userSetpoint_ - sp) > 0.001f) { userSetpoint_ = sp; changed = true; }
        }
        mode = mode_;
        setpoint = userSetpoint_;
    }
    if (changed)
    {
        modeSetting_.Set((uint32_t)mode);
        setpointSetting_.Set(setpoint);
        serviceProvider_.getSettingsManager().Save();
    }
    WriteStatus(out);
}

void ClimateManager::Cmd_ClimateStatus(Stream &, Stream &out)
{
    WriteStatus(out);
}

void ClimateManager::WriteStatus(Stream &out)
{
    ClimateMode mode;
    float userSp, room, activeSp, output, tset;
    bool  roomValid, ch, cool, ovr;
    {
        LOCK(mutex_);
        mode = mode_;
        userSp = userSetpoint_;
        room = lastRoomTemp_;
        roomValid = lastRoomValid_;
        activeSp = lastActiveSetpoint_;
        output = lastPidOutput_;
        tset = lastTSet_;
        ch = lastChEnable_;
        cool = lastCoolEnable_;
        ovr = lastOverrideActive_;
    }
    JsonObject resp(out);
    resp.field("mode", ClimateModeName(mode));
    resp.field("userSetpoint", userSp);
    resp.field("activeSetpoint", activeSp);
    resp.field("roomTemp", room);
    resp.field("roomValid", roomValid);
    resp.field("pidOutput", output);
    resp.field("tSet", tset);
    resp.field("chEnable", ch);
    resp.field("coolEnable", cool);
    resp.field("overrideActive", ovr);
}
```

- [ ] **Step 3: Declare it in ServiceProvider**

In `main/Application/ServiceProvider.h`, add the forward declaration after `class ClimateManager;`'s natural alphabetical spot (after `class Board;` group — put it right after `class Board;`):

```cpp
class ClimateManager;
```

and the accessor (after `getBoard()`):

```cpp
    virtual ClimateManager& getClimateManager() = 0;
```

- [ ] **Step 4: Own it in ApplicationContext**

In `main/Application/ApplicationContext.h`:

Add the include after the OpenThermManager include:
```cpp
#include "ClimateManager/ClimateManager.h"
```

Add the accessor (after `getBoard()`):
```cpp
    ClimateManager& getClimateManager() override { return m_climateManager; }
```

Add the member after `OpenThermManager m_openThermManager{*this};`:
```cpp
    ClimateManager m_climateManager{*this};
```

- [ ] **Step 5: Init it in main.cpp**

In `main/main.cpp`, between the OpenTherm and Update init lines:

```cpp
    g_appContext.getOpenThermManager().Init();
    g_appContext.getClimateManager().Init();
    g_appContext.getUpdateManager().Init();
```

- [ ] **Step 6: Register sources in CMake**

In `main/CMakeLists.txt`, add to `SOURCE_FILES_LIST`:
```cmake
    "Application/ClimateManager/ClimateManager.cpp"
```
and to `INCLUDE_DIRS_LIST`:
```cmake
    "Application/ClimateManager"
```

- [ ] **Step 7: Build**

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py build
```
Expected: `Project build complete`.

- [ ] **Step 8: Commit**

```bash
git add main/Application/ClimateManager/ClimateManager.cpp main/Application/ClimateManager/ClimateManager.h main/Application/ServiceProvider.h main/Application/ApplicationContext.h main/main.cpp main/CMakeLists.txt
git commit -m "Add ClimateManager: deadband PID heating, on/off cooling, frost-safe Off, ID 9 override adoption, climateSet/climateStatus"
```

---

### Task 4: Hardware verification

**Files:** none (controller runs this on the bench).

**Interfaces:** flashed firmware from Tasks 1–3; thermostat `192.168.50.63` (HTTP command API, password `admin`); gateway `192.168.50.189` (KC1/TCP:31600 via `scratchpad/kc_tcp.py`).

- [ ] **Step 1: Flash**

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py -p COM13 flash
```

- [ ] **Step 2: Heating curve**

`climateSet {"mode":"heat","setpoint":26}` (room ~24) → `climateStatus` shows `pidOutput` rising toward 1 and `tSet` in [30,80]; gateway `HeatingActive`, KC1 `DTMP SET` tracks the pushed room setpoint (26). Then `climateSet {"setpoint":20}` (below room) → `pidOutput` falls to ~0 and `tSet` 0.

- [ ] **Step 3: Cooling**

`climateSet {"mode":"cool","setpoint":20}` with room > 20.5 → `climateStatus` `coolEnable:true` (needs gateway `smarthome.coolingSupportEnabled`), `chEnable:false`, `tSet:0`.

- [ ] **Step 4: Off / frost (review + optional)**

`climateSet {"mode":"off"}` → `climateStatus` `chEnable:false`, `coolEnable:false` at normal room temp. Frost engagement (room < 5 °C) is not bench-reachable — verify by review; optionally confirm by a throwaway build with `kFrostSetpointC` set above room temp and seeing `chEnable:true`/`tSet>0` (revert before merge).

- [ ] **Step 5: Override**

Push a setpoint from the gateway (KC1 `CTMP SET<value>`, which the gateway relays as ID 9) → `climateStatus` `overrideActive:true`, `activeSetpoint` = pushed value; gateway `DTMP SET` echoes it (ID 16).

- [ ] **Step 6: Persistence**

`climateSet {"mode":"heat","setpoint":22}`, reboot the board, then `climateStatus` → `mode:"heat"`, `userSetpoint:22`.
