# OpenTherm Link + Master Manager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The thermostat speaks OpenTherm as master to the gateway — live room temp/setpoint/CH+DHW+cooling demand on the wire, boiler status/diagnostics read back, ID 9 remote override honored, link supervised — bench-controllable via `otSet`/`otStatus` console commands.

**Architecture:** Drop the `esp32_devkit` example board so `diyless_thermostat_3` is the only (default) target — no mocks, one build dir. New 3-method `OtLink` role interface; the proven `Stm32OpenThermLink` (ported from `feature/ot-thermostat-dropin`) implements it, hiding the STM32's CpuStatus-heartbeat quirk inside `Transaction()`. New `OpenThermManager` runs a 500 ms master loop: Status keepalive every cycle + ID 9 override read every other cycle + one slot from a fixed 14-slot write/read rotation. Demand is pushed in (`SetDemand`/commands), boiler state pulled out (`GetState`).

**Tech Stack:** ESP-IDF v6.0 (EIM), FreeRTOS task via `lib/rtos` wrappers, UART driver, JsonScope/JsonReader, CommandEntry tables.

## Global Constraints

- Repo: `c:\Workspace\KC1245 Gateway workspace\Thermostat`; branch `feature/opentherm-master` off `main`. Push to origin.
- Spec: `docs/superpowers/specs/2026-07-06-opentherm-link-manager-design.md` (incl. devkit-drop amendment). Sketches in `docs/sketches/opentherm/` are deleted by Task 3.
- Ported file source is branch `feature/ot-thermostat-dropin` — port via `git show`, then apply listed edits only.
- ESP-IDF activation per PowerShell call: `$env:PYTHONUTF8="1"; $env:PYTHONIOENCODING="utf-8"; . "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"`. Never `export.ps1`. Build timeout 600000 ms.
- After Task 1 there is ONE build flavor: default `build/` dir, target esp32s3, board `diyless_thermostat_3`. No `-B`/`-DBOARD`/`-DSDKCONFIG` flags anywhere.
- No automated test suite; verification = build green per task, hardware smoke in Task 4 (T3 on COM13; console is USB-Serial/JTAG), gateway E2E in Task 5 (user-assisted).
- Style: C++17, no exceptions, `snprintf` bounds, managers take `ServiceProvider&`, `InitState` guard, copy/move deleted.
- Commit messages end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

### Task 1: Drop esp32_devkit + Led example; diyless becomes the default board

**Files:**
- Delete: `main/hardware/boards/esp32_devkit/` (Board.h, Board.cpp, BoardConfig.h, board.cmake), `main/hardware/interfaces/Led.h`, `main/hardware/drivers/GpioLed.h`, `main/hardware/drivers/MockLed.h`
- Modify: `CMakeLists.txt` (root), `main/CMakeLists.txt`, `.gitignore`, `CLAUDE.md`, `main/hardware/boards/diyless_thermostat_3/board.cmake`

**Interfaces:**
- Produces: default `BOARD` = `diyless_thermostat_3`; single esp32s3 build in `build/`. Later tasks build with plain `idf.py build`.

- [ ] **Step 1: Branch + delete the example hardware**

```bash
cd "/c/Workspace/KC1245 Gateway workspace/Thermostat"
git checkout -b feature/opentherm-master main
git rm -r main/hardware/boards/esp32_devkit
git rm main/hardware/interfaces/Led.h main/hardware/drivers/GpioLed.h main/hardware/drivers/MockLed.h
grep -rn "GetLed\|GpioLed\|MockLed\|interfaces/Led.h" main || echo "no stale references"
```

Expected: grep prints `no stale references` (the Led role had no consumers).

- [ ] **Step 2: Make diyless the default board**

In root `CMakeLists.txt` change:
```cmake
if(NOT DEFINED BOARD)
    set(BOARD "esp32_devkit")
endif()
```
to:
```cmake
if(NOT DEFINED BOARD)
    set(BOARD "diyless_thermostat_3")
endif()
```

In `main/CMakeLists.txt` change:
```cmake
set(BOARD "esp32_devkit" CACHE STRING "Target board (folder under main/hardware/boards/)")
```
to:
```cmake
set(BOARD "diyless_thermostat_3" CACHE STRING "Target board (folder under main/hardware/boards/)")
```

- [ ] **Step 3: Simplify the build docs**

`CLAUDE.md` (repo root): in the firmware build block, change `idf.py set-target esp32` to `idf.py set-target esp32s3`, delete the line `idf.py -DBOARD=<name> build           # select a board from main/hardware/boards/ (default: esp32_devkit)` and DELETE the whole "The DIYLESS Thermostat 3 target is ESP32-S3 and must use its own build dir…" block (including its fenced commands and the `-DSDKCONFIG` explanation sentence) — the DIYLESS board is now simply the default:

```markdown
```bash
idf.py set-target esp32s3
idf.py build                          # DIYLESS Thermostat 3 (the only board)
idf.py -p <PORT> flash monitor        # console is on USB-Serial/JTAG
```
```

Also update the "boards" sentence under Layer separation if it names `esp32_devkit` as an example — replace the example name with `diyless_thermostat_3`.

`.gitignore`: delete the two lines `build_diyless/` and `sdkconfig_diyless*` (nothing generates them anymore).

`main/hardware/boards/diyless_thermostat_3/board.cmake`: replace the two "Build with:" comment lines with one line:
```cmake
# Build with:  idf.py set-target esp32s3   (once)   then   idf.py build
```

- [ ] **Step 4: Fresh single-flavor build**

PowerShell (repo root, activation prefix per Global Constraints):
```powershell
Remove-Item -Recurse -Force build, build_diyless -ErrorAction SilentlyContinue
Remove-Item -Force sdkconfig, sdkconfig_diyless, sdkconfig_diyless.old -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py build
```

Expected: configure log shows `Building for BOARD=diyless_thermostat_3`; `Project build complete.`; app partition 0x300000 (the board's sdkconfig.defaults + partitions_8mb.csv apply via the existing composition).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Drop esp32_devkit example board; diyless_thermostat_3 is the product

One target, one build dir: no more -B/-DSDKCONFIG variant juggling.
The Led example (interface + drivers) goes with it - no consumers.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

### Task 2: OtLink interface + Stm32OpenThermLink port + Board wiring

**Files:**
- Create: `main/hardware/interfaces/OtLink.h`
- Create: `main/hardware/drivers/Stm32OpenThermLink.h` (port + edits)
- Modify: `main/hardware/boards/diyless_thermostat_3/Board.h`, `Board.cpp`

**Interfaces:**
- Consumes: `BoardConfig::OT_UART_TX/OT_UART_RX/OT_STM32_BOOT0/OT_STM32_NRST` (already in BoardConfig.h).
- Produces: `class OtLink { bool Ready() const; bool Transaction(uint32_t request, uint32_t &response); bool Recover(); }`; `Board::GetOtLink() -> OtLink&`; driver public extras used by Board: `bool Init(uart_port_t, int tx, int rx, int boot0, int nrst, bool autoReset=true)`, `bool Handshake()`.

- [ ] **Step 1: Write the role interface**

`main/hardware/interfaces/OtLink.h`:
```cpp
#pragma once
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Role interface: transport for one OpenTherm frame exchange.
// Implemented by Stm32OpenThermLink (the STM32L051 co-processor
// owns the OT PHY). Consumers never see UARTs, resets or the
// STM32's heartbeat quirk — one call, one frame exchange.
// ──────────────────────────────────────────────────────────────
class OtLink
{
public:
    // Co-processor handshook and recently responsive.
    virtual bool Ready() const = 0;

    // One master->slave exchange: send a 32-bit OT frame, block for the
    // reply (sub-second). false = link/timeout error. An UNKNOWN-DATAID
    // reply is a SUCCESSFUL transaction — it arrives in `response`.
    virtual bool Transaction(uint32_t request, uint32_t &response) = 0;

    // Re-reset the STM32 into its app and redo the handshake (~1 s).
    virtual bool Recover() = 0;

    virtual ~OtLink() = default;
};
```

- [ ] **Step 2: Port the driver and adapt it to OtLink**

```bash
git show feature/ot-thermostat-dropin:main/hardware/drivers/Stm32OpenThermLink.h > main/hardware/drivers/Stm32OpenThermLink.h
```

Then apply exactly these edits with the Edit tool:

Edit A — include + inheritance. After the existing `#include <cstring>` add:
```cpp
#include "interfaces/OtLink.h"
```
and change `class Stm32OpenThermLink` to `class Stm32OpenThermLink : public OtLink`.

Edit B — add the OtLink implementation + `Handshake()`. Immediately after the `CpuStatus` struct's closing `};` (still in the public section), insert:
```cpp
    // ── OtLink role implementation ────────────────────────────
    // The STM32 stops servicing OtCommandRequests unless it has seen a
    // recent CpuStatus exchange (bring-up finding, RA2-398). Transaction()
    // refreshes that heartbeat transparently so callers never know.

    bool Ready() const override { return ready_ && handshakeOk_; }

    bool Transaction(uint32_t request, uint32_t &response) override
    {
        if (!ready_) return false;
        int64_t now = esp_timer_get_time();
        if (now - lastHeartbeatUs_ > HeartbeatPeriodUs)
        {
            CpuStatus cpu;
            if (!ReadCpuStatus(cpu)) { handshakeOk_ = false; return false; }
            handshakeOk_ = true;
            lastHeartbeatUs_ = esp_timer_get_time();
        }
        uint8_t status = 0;
        return Transact(request, response, status);
    }

    bool Recover() override
    {
        if (!ready_) return false;
        ResetIntoApp();
        return Handshake();
    }

    // First hello after reset; logs the co-processor versions once.
    bool Handshake()
    {
        CpuStatus cpu;
        handshakeOk_ = ReadCpuStatus(cpu);
        if (handshakeOk_)
        {
            lastHeartbeatUs_ = esp_timer_get_time();
            ESP_LOGI(TAG, "STM32 co-processor cpuVer=%u fwVer=%u boardRev=%u",
                     cpu.cpuVer, cpu.fwVer, cpu.boardRev);
        }
        return handshakeOk_;
    }
```

Edit C — add the two state members next to the existing `bool ready_ = false;` private member (search for it near the bottom):
```cpp
    bool    handshakeOk_ = false;
    int64_t lastHeartbeatUs_ = 0;
    static constexpr int64_t HeartbeatPeriodUs = 1000000;  // 1 s
```

Do NOT otherwise modify the ported protocol code (framing, Transact, ReadCpuStatus, ResetIntoApp stay byte-identical).

- [ ] **Step 3: Board owns the link**

`main/hardware/boards/diyless_thermostat_3/Board.h` — add after the existing `#include "driver/i2c_master.h"`:
```cpp
#include "drivers/Stm32OpenThermLink.h"
#include "interfaces/OtLink.h"
```
add the accessor next to the sensor accessors:
```cpp
    OtLink &GetOtLink() { return otLink_; }
```
and the member after `Aht20Sensor ambientSensor_;`:
```cpp
    Stm32OpenThermLink otLink_;
```

`Board.cpp` — append inside `Init()` after the AHT20 block, before `init.SetReady();`:
```cpp
    // STM32L051 OpenTherm co-processor (owns the OT PHY). Init resets it
    // into its app (~900 ms warm-up) and Handshake says hello.
    if (otLink_.Init(UART_NUM_1, BoardConfig::OT_UART_TX, BoardConfig::OT_UART_RX,
                     BoardConfig::OT_STM32_BOOT0, BoardConfig::OT_STM32_NRST))
    {
        if (!otLink_.Handshake())
            ESP_LOGW(TAG, "STM32 OT co-processor not responding (manager will retry)");
    }
    else
    {
        ESP_LOGE(TAG, "OT link UART init failed");
    }
```
and add `#include "driver/uart.h"` at the top of Board.cpp if not already pulled in transitively (the driver header includes it — verify by building).

- [ ] **Step 4: Build**

PowerShell: `idf.py build` → `Project build complete.`

- [ ] **Step 5: Commit**

```bash
git add main/hardware/interfaces/OtLink.h main/hardware/drivers/Stm32OpenThermLink.h main/hardware/boards/diyless_thermostat_3
git commit -m "Port Stm32OpenThermLink behind new OtLink role interface

Heartbeat quirk (STM32 needs recent CpuStatus to serve OT requests)
now lives inside Transaction(). Board resets + handshakes at Init.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

### Task 3: OpenThermManager + OtFrame + commands + wiring

**Files:**
- Create: `main/Application/OpenThermManager/OtFrame.h`, `OpenThermManager.h`, `OpenThermManager.cpp`
- Modify: `main/lib/json/JsonReader.h` (add `GetFloat`), `main/Application/ServiceProvider.h`, `main/Application/ApplicationContext.h`, `main/main.cpp`, `main/CMakeLists.txt`
- Delete: `docs/sketches/opentherm/` (real headers replace the sketches)

**Interfaces:**
- Consumes: `OtLink` via `serviceProvider_.getBoard().GetOtLink()` (Task 2); `TemperatureSensor` via `GetTemperatureSensor()`; `CommandManager::Register(this, commands_)`; `JsonObject(out).field(...)`; `JsonReader<256>(in).GetFloat/GetInt`.
- Produces: `OpenThermManager` with `void Init()`, `OtBoilerState GetState() const`, `OtDemand GetDemand() const`, `void SetDemand(const OtDemand&)`; commands `otStatus`, `otSet`; `serviceProvider.getOpenThermManager()`.

- [ ] **Step 1: JsonReader::GetFloat**

In `main/lib/json/JsonReader.h`, add `#include <cstdlib>` after `#include <cstdint>`, and after `GetBool` add:
```cpp
    float GetFloat(const char* key, float def = 0.0f) const
    {
        const char* v = FindJsonField(buf_, key);
        if (!v) return def;
        char* end = nullptr;
        float f = strtof(v, &end);
        return end == v ? def : f;
    }
```

- [ ] **Step 2: OtFrame.h (frame codec, manager-local)**

`main/Application/OpenThermManager/OtFrame.h`:
```cpp
#pragma once
#include <cstdint>
#include <cmath>

// 32-bit OpenTherm frame helpers: [31]=even parity  [30:28]=msg type
// [27:24]=spare  [23:16]=data-ID  [15:0]=data value. f8.8 = signed
// fixed-point temperature encoding.
namespace OtFrame
{
    enum MsgType : uint8_t
    {
        ReadData      = 0,
        WriteData     = 1,
        InvalidData   = 2,
        ReadAck       = 4,
        WriteAck      = 5,
        DataInvalid   = 6,
        UnknownDataId = 7,
    };

    inline uint32_t Build(MsgType type, uint8_t id, uint16_t value)
    {
        uint32_t f = ((uint32_t)(type & 0x7) << 28) | ((uint32_t)id << 16) | value;
        uint32_t v = f;
        int ones = 0;
        while (v) { ones += v & 1; v >>= 1; }
        if (ones & 1) f |= 0x80000000u;   // even parity over all 32 bits
        return f;
    }

    inline MsgType  Type(uint32_t f)  { return (MsgType)((f >> 28) & 0x7); }
    inline uint8_t  Id(uint32_t f)    { return (f >> 16) & 0xFF; }
    inline uint16_t Value(uint32_t f) { return f & 0xFFFF; }

    inline uint16_t F88(float v)      { return (uint16_t)(int16_t)lroundf(v * 256.0f); }
    inline float    FromF88(uint16_t v) { return (int16_t)v / 256.0f; }
}
```

- [ ] **Step 3: OpenThermManager.h**

```cpp
#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include <cstdint>

// What WE demand from the boiler (thermostat = OT master). Pushed in via
// SetDemand() — bench: the otSet command; later: ClimateManager's PID.
struct OtDemand
{
    bool  chEnable     = false;   // ID 0 master bit 0 — heat demand
    bool  dhwEnable    = false;   // ID 0 master bit 1
    bool  coolEnable   = false;   // ID 0 master bit 2 — cooling demand
    float roomSetpoint = 20.0f;   // °C → ID 16 (adopted from ID 9 on override)
    float dhwSetpoint  = 60.0f;   // °C → ID 56
    float tSet         = 0.0f;    // °C → ID 1 (later: the PID output)
};

// What the boiler/gateway reports back. Single source of boiler truth;
// consumers pull snapshots via GetState().
struct OtBoilerState
{
    bool linked = false;                 // recent Status exchange succeeded

    // slave status bits (ID 0 reply, low byte)
    bool fault = false;
    bool chActive = false;
    bool dhwActive = false;
    bool flame = false;
    bool coolingActive = false;

    // slave config (ID 3 high byte) — capabilities
    bool dhwPresent = false;
    bool coolingSupported = false;

    // sensors (f8.8 unless noted)
    float boilerTemp = 0;                // ID 25
    float returnTemp = 0;                // ID 28
    float dhwTemp = 0;                   // ID 26
    float modulation = 0;                // ID 17 (%)
    float chPressure = 0;                // ID 18 (bar)
    float outsideTemp = 0;               // ID 27

    // diagnostics (u16)
    uint16_t oemFaultCode = 0;           // ID 5
    uint16_t oemDiagCode  = 0;           // ID 115

    // t_set clamp from the boiler (ID 57, s8/s8 upper/lower)
    float maxTSetUpper = 80;
    float maxTSetLower = 30;
};

// OpenTherm MASTER (gateway = slave/boiler emulator). 500 ms cycle:
// Status keepalive + ID 9 override read every other cycle + one slot of a
// fixed write/read rotation. See docs/superpowers/specs/
// 2026-07-06-opentherm-link-manager-design.md for the normative schedule.
class OpenThermManager
{
    static constexpr const char *TAG = "OpenThermManager";
    static constexpr int  LoopDelayMs   = 500;
    static constexpr int  LinkFailLimit = 6;      // ~3 s → linked=false
    static constexpr int  RetryUnsupportedEvery = 120;  // rotation passes

public:
    explicit OpenThermManager(ServiceProvider &serviceProvider);

    OpenThermManager(const OpenThermManager &) = delete;
    OpenThermManager &operator=(const OpenThermManager &) = delete;
    OpenThermManager(OpenThermManager &&) = delete;
    OpenThermManager &operator=(OpenThermManager &&) = delete;

    void Init();

    OtBoilerState GetState() const;
    OtDemand      GetDemand() const;
    void          SetDemand(const OtDemand &d);   // ClimateManager's future entry

private:
    void Loop();
    bool DoStatus(class OtLink &link);            // ID 0 exchange + state update
    void DoOverrideRead(class OtLink &link);      // ID 9
    void DoRotationSlot(class OtLink &link);      // one write/read slot
    void Cmd_Status(Stream &in, Stream &out);     // otStatus
    void Cmd_Set(Stream &in, Stream &out);        // otSet

    inline static CommandEntry commands_[] = {
        { "otStatus", &InvokeCommand<&OpenThermManager::Cmd_Status> },
        { "otSet",    &InvokeCommand<&OpenThermManager::Cmd_Set> },
    };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    OtDemand      demand_;
    OtBoilerState state_;
    bool demandDirty_ = false;    // set by SetDemand/otSet → jump the rotation

    uint32_t cycle_ = 0;
    size_t   slot_ = 0;
    int      failStreak_ = 0;
    int      recoverBackoffS_ = 5;
    int64_t  nextRecoverUs_ = 0;
};
```

- [ ] **Step 4: OpenThermManager.cpp**

```cpp
#include "OpenThermManager.h"
#include "OtFrame.h"
#include "CommandManager/CommandManager.h"
#include "Board.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

using F = OtFrame::MsgType;

namespace
{
    // OpenTherm data-IDs (subset used here; mirrors gateway OTHThermostatProps).
    constexpr uint8_t ID_STATUS   = 0;
    constexpr uint8_t ID_TSET     = 1;
    constexpr uint8_t ID_SCONFIG  = 3;
    constexpr uint8_t ID_OEMFAULT = 5;
    constexpr uint8_t ID_TROVRD   = 9;
    constexpr uint8_t ID_TRSET    = 16;
    constexpr uint8_t ID_RELMOD   = 17;
    constexpr uint8_t ID_CHPRESS  = 18;
    constexpr uint8_t ID_TROOM    = 24;
    constexpr uint8_t ID_TBOILER  = 25;
    constexpr uint8_t ID_TDHW     = 26;
    constexpr uint8_t ID_TOUTSIDE = 27;
    constexpr uint8_t ID_TRET     = 28;
    constexpr uint8_t ID_TDHWSET  = 56;
    constexpr uint8_t ID_MAXTSET_BOUNDS = 57;
    constexpr uint8_t ID_OEMDIAG  = 115;

    // Fixed rotation: writes interleaved with reads. One slot per cycle.
    struct Slot { uint8_t id; bool write; };
    constexpr Slot kRotation[] = {
        { ID_TSET,     true  }, { ID_RELMOD,   false },
        { ID_TRSET,    true  }, { ID_TBOILER,  false },
        { ID_TROOM,    true  }, { ID_TDHW,     false },
        { ID_TDHWSET,  true  }, { ID_TRET,     false },
        { ID_CHPRESS,  false }, { ID_TOUTSIDE, false },
        { ID_OEMFAULT, false }, { ID_OEMDIAG,  false },
        { ID_MAXTSET_BOUNDS, false }, { ID_SCONFIG, false },
    };
    constexpr size_t kSlots = sizeof(kRotation) / sizeof(kRotation[0]);

    // Unsupported-ID bookkeeping (UNKNOWN-DATAID reply → rare retry).
    bool     unsupported[kSlots] = {};
    uint32_t pass = 0;
}

OpenThermManager::OpenThermManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void OpenThermManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);

    task_.Init("opentherm", 5, 8192);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (OpenTherm master, link %s)",
             serviceProvider_.getBoard().GetOtLink().Ready() ? "ready" : "not ready");
}

OtBoilerState OpenThermManager::GetState() const { LOCK(mutex_); return state_; }
OtDemand      OpenThermManager::GetDemand() const { LOCK(mutex_); return demand_; }

void OpenThermManager::SetDemand(const OtDemand &d)
{
    LOCK(mutex_);
    demand_ = d;
    demandDirty_ = true;
}

// ── the master loop ───────────────────────────────────────────

void OpenThermManager::Loop()
{
    OtLink &link = serviceProvider_.getBoard().GetOtLink();
    bool lastLinked = false;

    while (true)
    {
        if (link.Ready())
        {
            bool ok = DoStatus(link);
            if (ok)
            {
                failStreak_ = 0;
                recoverBackoffS_ = 5;
                if ((cycle_ & 1) == 0)
                    DoOverrideRead(link);
                DoRotationSlot(link);
            }
            else if (++failStreak_ >= LinkFailLimit)
            {
                LOCK(mutex_);
                state_.linked = false;
            }
        }
        else
        {
            // Link down: safe state (no frames = no demand). Recover with backoff.
            { LOCK(mutex_); state_.linked = false; }
            int64_t now = esp_timer_get_time();
            if (now >= nextRecoverUs_)
            {
                if (!link.Recover())
                {
                    nextRecoverUs_ = now + (int64_t)recoverBackoffS_ * 1000000;
                    recoverBackoffS_ = recoverBackoffS_ >= 30 ? 30 : recoverBackoffS_ * 2;
                }
                else
                {
                    recoverBackoffS_ = 5;
                }
            }
        }

        // Log link transitions once.
        {
            LOCK(mutex_);
            if (state_.linked != lastLinked)
            {
                ESP_LOGI(TAG, "OT link %s", state_.linked ? "up" : "down");
                lastLinked = state_.linked;
            }
        }

        cycle_++;
        vTaskDelay(pdMS_TO_TICKS(LoopDelayMs));
    }
}

bool OpenThermManager::DoStatus(OtLink &link)
{
    uint16_t master;
    {
        LOCK(mutex_);
        master = (uint16_t)(((demand_.chEnable   ? 1 : 0) << 0 |
                             (demand_.dhwEnable  ? 1 : 0) << 1 |
                             (demand_.coolEnable ? 1 : 0) << 2) << 8);
    }
    uint32_t reply = 0;
    if (!link.Transaction(OtFrame::Build(F::ReadData, ID_STATUS, master), reply) ||
        OtFrame::Type(reply) != F::ReadAck)
        return false;

    uint8_t slave = OtFrame::Value(reply) & 0xFF;
    LOCK(mutex_);
    state_.linked        = true;
    state_.fault         = slave & 0x01;
    state_.chActive      = slave & 0x02;
    state_.dhwActive     = slave & 0x04;
    state_.flame         = slave & 0x08;
    state_.coolingActive = slave & 0x10;
    return true;
}

void OpenThermManager::DoOverrideRead(OtLink &link)
{
    uint32_t reply = 0;
    if (!link.Transaction(OtFrame::Build(F::ReadData, ID_TROVRD, 0), reply) ||
        OtFrame::Type(reply) != F::ReadAck)
        return;

    float ovr = OtFrame::FromF88(OtFrame::Value(reply));
    if (ovr <= 0.0f) return;   // 0 = no override pending

    LOCK(mutex_);
    if (fabsf(ovr - demand_.roomSetpoint) > 0.05f)
    {
        ESP_LOGI(TAG, "Remote override: setpoint %.1f -> %.1f", demand_.roomSetpoint, ovr);
        demand_.roomSetpoint = ovr;   // ID 16 echoes it from the next cycles
        demandDirty_ = true;
    }
}

void OpenThermManager::DoRotationSlot(OtLink &link)
{
    // A dirty demand jumps the queue: restart at slot 0 so the writes
    // (t_set, setpoints) go out within ~2 s of the change.
    {
        LOCK(mutex_);
        if (demandDirty_) { slot_ = 0; demandDirty_ = false; }
    }

    // Skip unsupported IDs except on the rare retry pass.
    size_t tries = 0;
    while (unsupported[slot_] && (pass % RetryUnsupportedEvery) != 0 && tries++ < kSlots)
        if (++slot_ >= kSlots) { slot_ = 0; pass++; }

    const Slot s = kRotation[slot_];
    size_t idx = slot_;
    if (++slot_ >= kSlots) { slot_ = 0; pass++; }

    uint32_t req;
    if (s.write)
    {
        float v;
        {
            LOCK(mutex_);
            switch (s.id)
            {
            case ID_TSET:
                v = demand_.tSet;
                if (v < state_.maxTSetLower) v = (demand_.chEnable && v > 0) ? state_.maxTSetLower : 0;
                if (v > state_.maxTSetUpper) v = state_.maxTSetUpper;
                break;
            case ID_TRSET:   v = demand_.roomSetpoint; break;
            case ID_TDHWSET: v = demand_.dhwSetpoint;  break;
            case ID_TROOM:
            default:         v = NAN; break;   // filled below without the lock
            }
        }
        if (s.id == ID_TROOM)
        {
            // Temporary feed until ClimateManager owns the (calibrated) value.
            float t = 0;
            if (!serviceProvider_.getBoard().GetTemperatureSensor().ReadTemperature(t))
                return;   // no valid sample -> skip this slot
            v = t;
        }
        req = OtFrame::Build(F::WriteData, s.id, OtFrame::F88(v));
    }
    else
    {
        req = OtFrame::Build(F::ReadData, s.id, 0);
    }

    uint32_t reply = 0;
    if (!link.Transaction(req, reply)) return;

    if (OtFrame::Type(reply) == F::UnknownDataId)
    {
        if (!unsupported[idx])
            ESP_LOGI(TAG, "Data-ID %u not supported by slave", s.id);
        unsupported[idx] = true;
        return;
    }
    unsupported[idx] = false;
    if (s.write) return;                        // WriteAck carries nothing to store
    if (OtFrame::Type(reply) != F::ReadAck) return;

    uint16_t val = OtFrame::Value(reply);
    LOCK(mutex_);
    switch (s.id)
    {
    case ID_RELMOD:   state_.modulation  = OtFrame::FromF88(val); break;
    case ID_TBOILER:  state_.boilerTemp  = OtFrame::FromF88(val); break;
    case ID_TDHW:     state_.dhwTemp     = OtFrame::FromF88(val); break;
    case ID_TRET:     state_.returnTemp  = OtFrame::FromF88(val); break;
    case ID_CHPRESS:  state_.chPressure  = OtFrame::FromF88(val); break;
    case ID_TOUTSIDE: state_.outsideTemp = OtFrame::FromF88(val); break;
    case ID_OEMFAULT: state_.oemFaultCode = val; break;
    case ID_OEMDIAG:  state_.oemDiagCode  = val; break;
    case ID_MAXTSET_BOUNDS:
        state_.maxTSetUpper = (int8_t)(val >> 8);
        state_.maxTSetLower = (int8_t)(val & 0xFF);
        break;
    case ID_SCONFIG:
    {
        uint8_t cfg = val >> 8;
        state_.dhwPresent       = cfg & 0x01;
        state_.coolingSupported = cfg & 0x04;
        break;
    }
    }
}

// ── commands (web-UI console / WebSocket) ─────────────────────

void OpenThermManager::Cmd_Status(Stream &, Stream &out)
{
    OtBoilerState s;
    OtDemand d;
    {
        LOCK(mutex_);
        s = state_;
        d = demand_;
    }
    JsonObject resp(out);
    resp.field("linked", s.linked);
    resp.field("fault", s.fault);
    resp.field("chActive", s.chActive);
    resp.field("dhwActive", s.dhwActive);
    resp.field("flame", s.flame);
    resp.field("coolingActive", s.coolingActive);
    resp.field("dhwPresent", s.dhwPresent);
    resp.field("coolingSupported", s.coolingSupported);
    resp.field("boilerTemp", s.boilerTemp);
    resp.field("returnTemp", s.returnTemp);
    resp.field("dhwTemp", s.dhwTemp);
    resp.field("modulation", s.modulation);
    resp.field("chPressure", s.chPressure);
    resp.field("outsideTemp", s.outsideTemp);
    resp.field("oemFaultCode", (uint32_t)s.oemFaultCode);
    resp.field("oemDiagCode", (uint32_t)s.oemDiagCode);
    resp.field("maxTSetUpper", s.maxTSetUpper);
    resp.field("maxTSetLower", s.maxTSetLower);
    resp.field("chEnable", d.chEnable);
    resp.field("dhwEnable", d.dhwEnable);
    resp.field("coolEnable", d.coolEnable);
    resp.field("roomSetpoint", d.roomSetpoint);
    resp.field("dhwSetpoint", d.dhwSetpoint);
    resp.field("tSet", d.tSet);
}

void OpenThermManager::Cmd_Set(Stream &in, Stream &out)
{
    JsonReader<256> json(in);
    {
        LOCK(mutex_);
        float f;
        int   i;
        if (!std::isnan(f = json.GetFloat("setpoint", NAN)))    demand_.roomSetpoint = f;
        if (!std::isnan(f = json.GetFloat("dhwSetpoint", NAN))) demand_.dhwSetpoint = f;
        if (!std::isnan(f = json.GetFloat("tset", NAN)))        demand_.tSet = f;
        if ((i = json.GetInt("ch", -1))   >= 0) demand_.chEnable   = i != 0;
        if ((i = json.GetInt("dhw", -1))  >= 0) demand_.dhwEnable  = i != 0;
        if ((i = json.GetInt("cool", -1)) >= 0) demand_.coolEnable = i != 0;
        demandDirty_ = true;
    }
    Cmd_Status(in, out);   // reply with the resulting full state
}
```

- [ ] **Step 5: Wiring**

`main/Application/ServiceProvider.h` — add `class OpenThermManager;` to the forward declarations (alphabetical, after `class NetworkManager;`) and `virtual OpenThermManager& getOpenThermManager() = 0;` to the interface (after `getNetworkManager()`).

`main/Application/ApplicationContext.h` — add `#include "OpenThermManager/OpenThermManager.h"` (after NetworkManager include), accessor `OpenThermManager& getOpenThermManager() override { return m_openThermManager; }`, and member `OpenThermManager m_openThermManager{*this};` placed AFTER `Board m_board{*this};` in the member list (members construct in declaration order; the manager's task must not outrace Board — Init order is what matters, but keep the declaration order sensible).

`main/main.cpp` — add after `g_appContext.getBoard().Init();`:
```cpp
    g_appContext.getOpenThermManager().Init();
```

`main/CMakeLists.txt` — add to `SOURCE_FILES_LIST`:
```cmake
    "Application/OpenThermManager/OpenThermManager.cpp"
```
and to `INCLUDE_DIRS_LIST`:
```cmake
    "Application/OpenThermManager"
```

- [ ] **Step 6: Delete the design sketches (superseded by the real headers)**

```bash
git rm -r docs/sketches/opentherm
```

- [ ] **Step 7: Build**

PowerShell: `idf.py build` → `Project build complete.`

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "Add OpenThermManager: OT master loop with full ID schedule

500 ms cycle: Status keepalive + ID 9 override read + 14-slot
write/read rotation. Demand pushed in (otSet command for the bench,
ClimateManager later), boiler state pulled out (otStatus). Unknown
data-IDs demoted to rare retries; link supervision with backoff.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

### Task 4: Hardware smoke test + push

**Files:** none.

**Interfaces:**
- Consumes: T3 on COM13 (USB-Serial/JTAG; confirm the port with the user if flashing fails to connect).

- [ ] **Step 1: Flash**

PowerShell: `idf.py -p COM13 flash` → esptool writes and hard-resets.

- [ ] **Step 2: Capture the boot log**

Use the session's boot-capture approach (esptool reset + read COM13 ~14 s). Expected lines, in order:
- `Board: AHT20 ambient: <temp> degC`
- `OTLink: STM32 co-processor cpuVer=<n> fwVer=<n> boardRev=<n>` (the handshake — THE deliverable of the driver port)
- `Board: Initialized`
- `OpenThermManager: Initialized (OpenTherm master, link ready)`
- `main: All managers initialized, firmware confirmed valid`
- NO reboot loop; with no gateway wired the loop may log `OT link down` once after ~3 s (failed status exchanges) — that is correct supervision behavior, not a failure. There must be no repeated error spam (one transition log only).

- [ ] **Step 3: Push**

```bash
git push -u origin feature/opentherm-master
```

### Task 5: Gateway end-to-end validation (user-assisted — needs the OT wire + gateway)

**Files:** none. Requires: T3's OT terminals wired to the gateway's OT thermostat input; gateway running the `ram` build with THR=1; gateway reachable on the LAN (KC1/TCP:31600) for observation.

- [ ] **Step 1: Link up** — thermostat log shows `OT link up`; `otStatus` (web-UI console) shows `linked:true` and boiler fields populating.
- [ ] **Step 2: Gateway sees us** — via KC1/TCP: live room temp (ID 24) ≈ AHT20 value and room setpoint (ID 16) = 20.0 (default demand).
- [ ] **Step 3: Heat call** — `otSet {"ch":1,"setpoint":28,"tset":45}` → gateway `HeatingActive`; `otStatus` shows `chActive`/`flame` per gateway emulation.
- [ ] **Step 4: Override** — push a setpoint from the gateway side (reservation / smart-home / KC1) → thermostat logs `Remote override: setpoint 28.0 -> <x>`; gateway sees ID 16 echo within its override timeout.
- [ ] **Step 5: DHW + cooling bits** — `otSet {"dhw":1}` and `otSet {"cool":1}` (cooling only meaningful with gateway `smarthome.coolingSupportEnabled`); confirm the gateway's thermostat props reflect them as far as the emulator supports.
- [ ] **Step 6: Supervision** — pull the OT wire → `OT link down` within ~3 s, no spam; reconnect → `OT link up` recovers automatically.
- [ ] **Step 7: Merge decision** — report results; finishing-a-development-branch presents the options.
