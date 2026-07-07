# Room Temperature Manager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `RoomTemperatureManager` that owns the measured room temperature — fixed-cadence AHT20 sampling, 30 s validity, `roomTemp` bench command — with OpenThermManager consuming it for ID 24 instead of reading the sensor directly.

**Architecture:** Standard Strux manager (ServiceProvider DI, InitState, own Task). A 5 s sampling loop caches `{value, timestamp}` under a mutex; `GetRoomTemperature(float&)` returns false when the last good sample is older than 30 s — that is the fault signal. OpenThermManager's `GetWriteValue(ID_TROOM)` switches from the board sensor to this manager.

**Tech Stack:** ESP-IDF v6.0, C++17, FreeRTOS via `lib/rtos` wrappers (`Task`, `Mutex`, `LOCK`, `InitState`), `lib/json` (`JsonScope`), CommandManager `CommandEntry` tables.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-07-room-temperature-design.md` governs.
- Sampling cadence **5000 ms**; validity window **30 s** (`esp_timer_get_time()` based).
- Task name `"roomtemp"`, priority **5**, stack **4096**.
- Command name exactly `roomTemp`; reply fields exactly `valid` (bool), `temp` (float, last known, 0 if never read), `ageMs` (u32, 0 if never read).
- Init order in `main.cpp`: after `getBoard().Init()`, before `getOpenThermManager().Init()`.
- **No calibration, no humidity, no smoothing** — explicitly out of scope (see spec).
- Conventions: no `strcpy`/`strcat`, `snprintf` with bounds; managers copy/move-deleted; sources listed explicitly in `main/CMakeLists.txt` (no globbing).
- There are no automated tests in this repo; each task's test cycle is a green build (`idf.py build`), hardware verification is Task 3.
- Build (PowerShell): `. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py build` from the repo root. Never use export.ps1.

---

### Task 1: RoomTemperatureManager + wiring

**Files:**
- Create: `main/Application/RoomTemperatureManager/RoomTemperatureManager.h`
- Create: `main/Application/RoomTemperatureManager/RoomTemperatureManager.cpp`
- Modify: `main/Application/ServiceProvider.h`
- Modify: `main/Application/ApplicationContext.h`
- Modify: `main/main.cpp`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `Board::GetTemperatureSensor()` → `TemperatureSensor&` (role interface: `bool ReadTemperature(float &celsius)`); `CommandManager::Register(void*, CommandEntry[])`; `lib/rtos` `Task`/`Mutex`/`LOCK`/`InitState`.
- Produces: `RoomTemperatureManager::GetRoomTemperature(float &celsius) -> bool` and `ServiceProvider::getRoomTemperatureManager() -> RoomTemperatureManager&` — Task 2 relies on both, exactly these names.

- [ ] **Step 1: Create the header**

`main/Application/RoomTemperatureManager/RoomTemperatureManager.h`:

```cpp
#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include <cstdint>

// Owns "the measured room temperature": samples the board's ambient
// TemperatureSensor on a fixed cadence and serves cached, validity-checked
// snapshots. Consumers (OpenThermManager now; ClimateManager/UI later) never
// touch the sensor directly — a future external/BLE source swaps in behind
// GetRoomTemperature() without touching them. Calibration is deliberately
// absent (docs/backlog/room-temp-calibration.md).
class RoomTemperatureManager
{
    static constexpr const char *TAG = "RoomTemperatureManager";
    static constexpr int     SampleIntervalMs = 5000;
    static constexpr int64_t ValidityUs = 30LL * 1000 * 1000;  // 6 missed samples

public:
    explicit RoomTemperatureManager(ServiceProvider &serviceProvider);

    RoomTemperatureManager(const RoomTemperatureManager &) = delete;
    RoomTemperatureManager &operator=(const RoomTemperatureManager &) = delete;
    RoomTemperatureManager(RoomTemperatureManager &&) = delete;
    RoomTemperatureManager &operator=(RoomTemperatureManager &&) = delete;

    void Init();

    // false = no valid recent measurement (never read, or stale > 30 s).
    bool GetRoomTemperature(float &celsius);

private:
    void Loop();
    void Cmd_RoomTemp(Stream &in, Stream &out);   // roomTemp

    inline static CommandEntry commands_[] = {
        { "roomTemp", &InvokeCommand<&RoomTemperatureManager::Cmd_RoomTemp> },
    };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    float   lastTemp_ = 0.0f;     // last successful reading
    int64_t lastReadUs_ = -1;     // esp_timer time of it; -1 = never read
    bool    lastValid_ = false;   // loop-task-only edge detector for the fault log
};
```

- [ ] **Step 2: Create the implementation**

`main/Application/RoomTemperatureManager/RoomTemperatureManager.cpp`:

```cpp
#include "RoomTemperatureManager.h"
#include "CommandManager/CommandManager.h"
#include "Board.h"
#include "interfaces/TemperatureSensor.h"
#include "JsonScope.h"
#include "esp_log.h"
#include "esp_timer.h"

RoomTemperatureManager::RoomTemperatureManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void RoomTemperatureManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);

    task_.Init("roomtemp", 5, 4096);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (sampling every %d ms)", SampleIntervalMs);
}

bool RoomTemperatureManager::GetRoomTemperature(float &celsius)
{
    LOCK(mutex_);
    if (lastReadUs_ < 0) return false;
    if (esp_timer_get_time() - lastReadUs_ > ValidityUs) return false;
    celsius = lastTemp_;
    return true;
}

void RoomTemperatureManager::Loop()
{
    TemperatureSensor &sensor = serviceProvider_.getBoard().GetTemperatureSensor();

    while (true)
    {
        float t = 0;
        if (sensor.ReadTemperature(t))
        {
            LOCK(mutex_);
            lastTemp_ = t;
            lastReadUs_ = esp_timer_get_time();
        }

        // Log valid<->invalid transitions once (edge-detected, like the OT
        // manager's link logging). lastValid_/lastTemp_ race-free here: this
        // task is the only writer.
        float unused;
        bool valid = GetRoomTemperature(unused);
        if (valid != lastValid_)
        {
            if (valid) ESP_LOGI(TAG, "Room temp source restored (%.1f C)", lastTemp_);
            else       ESP_LOGW(TAG, "Room temp source lost (no valid sample for 30 s)");
            lastValid_ = valid;
        }

        vTaskDelay(pdMS_TO_TICKS(SampleIntervalMs));
    }
}

void RoomTemperatureManager::Cmd_RoomTemp(Stream &, Stream &out)
{
    float   temp;
    int64_t readUs;
    {
        LOCK(mutex_);
        temp   = lastTemp_;
        readUs = lastReadUs_;
    }
    int64_t now = esp_timer_get_time();
    bool     valid = readUs >= 0 && (now - readUs) <= ValidityUs;
    uint32_t ageMs = readUs < 0 ? 0 : (uint32_t)((now - readUs) / 1000);

    JsonObject resp(out);
    resp.field("valid", valid);
    resp.field("temp", temp);
    resp.field("ageMs", ageMs);
}
```

- [ ] **Step 3: Declare it in ServiceProvider**

In `main/Application/ServiceProvider.h`, add the forward declaration after `class OpenThermManager;` and the accessor after `getOpenThermManager()`:

```cpp
class RoomTemperatureManager;
```

```cpp
    virtual RoomTemperatureManager& getRoomTemperatureManager() = 0;
```

- [ ] **Step 4: Own it in ApplicationContext**

In `main/Application/ApplicationContext.h`:

Add the include after the OpenThermManager one:
```cpp
#include "RoomTemperatureManager/RoomTemperatureManager.h"
```

Add the accessor after `getOpenThermManager()`:
```cpp
    RoomTemperatureManager& getRoomTemperatureManager() override { return m_roomTemperatureManager; }
```

Add the member between `Board m_board{*this};` and `OpenThermManager m_openThermManager{*this};` (declaration order = init order in main.cpp):
```cpp
    RoomTemperatureManager m_roomTemperatureManager{*this};
```

- [ ] **Step 5: Init it in main.cpp**

In `main/main.cpp`, between the Board and OpenTherm lines:

```cpp
    g_appContext.getBoard().Init();
    g_appContext.getRoomTemperatureManager().Init();
    g_appContext.getOpenThermManager().Init();
```

- [ ] **Step 6: Register sources in CMake**

In `main/CMakeLists.txt`, add to `SOURCE_FILES_LIST` (next to the OpenThermManager entry):
```cmake
    "Application/RoomTemperatureManager/RoomTemperatureManager.cpp"
```
and to `INCLUDE_DIRS_LIST` (next to `"Application/OpenThermManager"`):
```cmake
    "Application/RoomTemperatureManager"
```

- [ ] **Step 7: Build**

Run (PowerShell, repo root):
```powershell
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py build
```
Expected: `Project build complete`.

- [ ] **Step 8: Commit**

```bash
git add main/Application/RoomTemperatureManager/ main/Application/ServiceProvider.h main/Application/ApplicationContext.h main/main.cpp main/CMakeLists.txt
git commit -m "Add RoomTemperatureManager: 5 s sampling, 30 s validity, roomTemp command"
```

---

### Task 2: OpenThermManager consumes the manager for ID 24

**Files:**
- Modify: `main/Application/OpenThermManager/OpenThermManager.cpp` (the `GetWriteValue` function, ~line 280)

**Interfaces:**
- Consumes: `ServiceProvider::getRoomTemperatureManager() -> RoomTemperatureManager&` and `RoomTemperatureManager::GetRoomTemperature(float &celsius) -> bool` (Task 1).
- Produces: nothing new — behavior contract: ID 24 rotation slot is skipped when no valid room temp (unchanged from before).

- [ ] **Step 1: Switch the ID 24 source**

In `main/Application/OpenThermManager/OpenThermManager.cpp`, add the include after `#include "Board.h"`:

```cpp
#include "RoomTemperatureManager.h"
```

Then in `GetWriteValue`, replace:

```cpp
    if (id == ID_TROOM)
    {
        // Temporary feed until ClimateManager owns the (calibrated) value.
        float t = 0;
        if (!serviceProvider_.getBoard().GetTemperatureSensor().ReadTemperature(t))
            return false;
        v = t;
        return true;
    }
```

with:

```cpp
    if (id == ID_TROOM)
    {
        // RoomTemperatureManager owns the measured value (validity-checked);
        // false -> the rotation slot is skipped, gateway keeps its last value.
        return serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(v);
    }
```

- [ ] **Step 2: Build**

Run (PowerShell, repo root):
```powershell
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py build
```
Expected: `Project build complete`.

- [ ] **Step 3: Commit**

```bash
git add main/Application/OpenThermManager/OpenThermManager.cpp
git commit -m "OpenThermManager: pull ID 24 room temp from RoomTemperatureManager"
```

---

### Task 3: Hardware verification

**Files:** none (verification only; done by the controller on the bench).

**Interfaces:**
- Consumes: flashed firmware from Tasks 1+2; thermostat at `192.168.50.63` (HTTP command API, password `admin`); gateway at `192.168.50.189` (KC1/TCP:31600 via `scratchpad/kc_tcp.py`).

- [ ] **Step 1: Flash**

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"; $env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; idf.py -p COM13 flash
```
Expected: `Done` after hard reset.

- [ ] **Step 2: Boot log shows the manager**

Boot log (COM13, or device log history via the `getLogs` command) contains
`RoomTemperatureManager: Initialized (sampling every 5000 ms)` and, within
~5 s, `Room temp source restored (...)` — the first valid sample's edge.

- [ ] **Step 3: roomTemp command**

`POST /api/command?type=roomTemp` (Bearer token from `/api/login`) returns
`valid:true`, `temp` plausible (bench ~20–30 °C), `ageMs < 5000` on repeat
calls.

- [ ] **Step 4: Gateway still sees ID 24**

`python kc_tcp.py DTMP ACT` on the gateway matches `roomTemp`'s `temp`
within ~0.5 °C (one rotation period of lag is fine). OT link stays
`linked:true` in `otStatus`.

- [ ] **Step 5: Fault path (review, not hardware)**

AHT20 is soldered on-board — the fault path is verified by review:
`GetRoomTemperature` returns false when `lastReadUs_ < 0` or the sample is
older than `ValidityUs`; `GetWriteValue(ID_TROOM)` then returns false and
the rotation skips the slot (established behavior).
