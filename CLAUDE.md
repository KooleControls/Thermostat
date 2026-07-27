# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

The **KC Thermostat** — an OpenTherm room thermostat (OT master) that pairs with the KC1245 Gateway as a drop-in replacement for the third-party unit. Built on the Strux template (ESP-IDF v6.0, C++, FreeRTOS, React web UI); Strux is a local git remote (`strux`) and template improvements flow both ways. Backlog: `docs/backlog/` (deliverables tracked in Jira: RA2-395, RA2-437, RA2-435).

Deviations from stock Strux: **no MQTT / Home Assistant managers** (the gateway owns smart-home integration).

**Open-source (approved 2026-07-27).** This repo will be released open-source, so it's a firm rule: **no KC-proprietary code here** — the connection-server protocol, keys, and fleet semantics live on the *gateway*, and the thermostat exposes only a generic, transport-agnostic command surface (`CommandManager`). The gateway translates KC intent into plain commands; the thermostat never knows it's "KC". Keep it that way.

## Build commands

Firmware (requires ESP-IDF v6.0+ environment):

```bash
idf.py set-target esp32s3
idf.py build                          # DIYLESS Thermostat 3 (the only board)
idf.py -p <PORT> flash monitor        # console is on USB-Serial/JTAG
```

Frontend (React 19 + TypeScript + Vite + Tailwind + shadcn/ui, package manager is pnpm):

```bash
cd frontend
pnpm dev          # hot-reload dev server, proxies WebSocket to a running device
pnpm build        # tsc -b && vite build && gzip into ../www (embedded in flash as FAT image)
pnpm typecheck    # tsc --noEmit
```

There are no automated tests; verification is building and flashing a device. `docs/backlog/` holds concrete software work planned to be done — that's the only tracking layer in this repo (no set-aside "ideas" layer; deliverables and undecided/maybe-someday items live in Jira).

## Architecture

### Manager pattern (dependency injection)

Everything in firmware is a "manager" owned by `ApplicationContext` ([main/Application/ApplicationContext.h](main/Application/ApplicationContext.h)), which implements the pure-virtual `ServiceProvider` interface ([main/Application/ServiceProvider.h](main/Application/ServiceProvider.h)). Every manager:

- takes `ServiceProvider&` in its constructor and reaches other managers through it (never directly),
- has copy/move deleted,
- initializes in `Init()` guarded by an `InitState` (`lib/rtos/InitState.h`), not in the constructor.

`main.cpp` is only ordered `Init()` calls — order matters (Console → Settings → System → Network → Time → Command → Board → RoomTemperature → OpenTherm → Climate → HotWater → Display → Update → WebServer). Adding a manager means: create the class, add it to `ServiceProvider`, `ApplicationContext`, `main.cpp`, and `main/CMakeLists.txt` (both `SOURCE_FILES_LIST` and `INCLUDE_DIRS_LIST` — sources are listed explicitly, no globbing).

### Layer separation

- `main/hardware/` — changes when you swap the board. Split into:
  - `boards/<name>/` — one folder per target board: `BoardConfig.h` (pins/constants), `Board.h`/`Board.cpp` (the board's `Board` class — owns every driver instance and bus host, exposes the capability surface the application compiles against; `Board.cpp` is added via `BOARD_SOURCES` in the `board.cmake` fragment), and an optional `sdkconfig.defaults` overlaying the common root one. Selected with `-DBOARD=<name>`; only the chosen board folder is on the include path, so `#include "BoardConfig.h"` and `#include "Board.h"` resolve to it. There is no `IBoard` base class — the contract is duck-typed: a board missing a method the application uses fails to compile for that board.
  - `interfaces/` — role interfaces in application vocabulary (`TemperatureSensor`, `OtLink`), 1–3 pure-virtual methods each, never chip or GPIO vocabulary. Drivers implement them (`Aht20Sensor : TemperatureSensor`, `Stm32OpenThermLink : OtLink`); a board without the hardware simply doesn't expose the accessor. Add a role interface only when application code speaks in that role; expose a concrete driver accessor from `Board` instead when the application needs a driver's full API (escape hatch). Multi-instance roles get a semantic enum (`Sensor::Ambient`, never `Sensor_2`) mapped by the board — introduce it with the first multi-instance role.
  - `drivers/` — board-independent chip/peripheral drivers shared by boards (e.g. `Stm32OpenThermLink.h`), taking pins/buses as constructor parameters (passed by the board from its `BoardConfig` constants).
- `main/Application/` — changes when you add a feature. Managers and business logic. Hardware driver *instances* live in the board's `Board` class, reached via `ServiceProvider::getBoard()`.
- `main/lib/` — stable building blocks: RTOS wrappers (`Task`, `Mutex`, `Timer`), `Stream`/`MemoryStream`/`BufferStream`, `JsonWriter`/`JsonReader`, `DateTime`/`TimeSpan`. Rarely changes.

Note: `board.cmake` fragments cannot change component `REQUIRES` (ESP-IDF resolves those in an early pass without the `BOARD` cache var). IDF built-in deps go in `COMPONENT_REQUIRES` in [main/CMakeLists.txt](main/CMakeLists.txt); managed components go in [main/idf_component.yml](main/idf_component.yml).

### Commands (the device's RPC surface)

`CommandManager` is a pure dispatcher — it knows no commands and no other managers. Each command lives in the manager that owns its domain:

- Handlers have the signature `void Handler(Stream& in, Stream& out)`: `in` carries the request payload, the handler writes its complete reply to `out`. Streams are the contract; JSON is a dialect the handler opts into by constructing `JsonReader`/`JsonObject` on line one. Binary payloads (e.g. firmware chunks) use the same contract.
- Owners declare an `inline static CommandEntry commands_[]` table ([CommandEntry.h](main/Application/CommandManager/CommandEntry.h)) with `InvokeCommand<&Owner::Method>` trampolines, and hand it to `CommandManager::Register()` from their `Init()`. Tables must have static storage duration — a registered entry that dies aborts with `FATAL`.
- Commands reach `Execute()` over **one transport: the WebSocket** (`/ws`), using
  the session-multiplexed binary protocol (`SessionProtocol.h`, spec
  `docs/superpowers/specs/2026-07-09-session-mux-transport-design.md`). Each frame
  is `[session:u16 LE][flags:u8][payload]`; a request is one `FLAG_FINAL` chunk
  whose payload is the command JSON (`{"type":…,…}` + `\n`), and the reply comes
  back on the same session id. HTTP (`StaticFileHandler`) now serves the static
  frontend only — `/api/command` and `/api/login` were retired in the Strux
  session-transport rework. Auth is per-connection, established in-band right
  after the socket opens (`hello` → `login`/`auth`); `WebServerManager` splits
  into `Authenticator`, `AuthGate`, `ConnectionRegistry`, and `SessionMux`.

Log lines broadcast to authenticated WebSocket clients via `ConsoleManager` on the
reserved broadcast session (0). The frontend side is a singleton `BackendService`
([frontend/src/lib/backend.ts](frontend/src/lib/backend.ts)) that matches replies
to requests by session id and auto-reconnects.

`UpdateManager`'s entire external surface is its command table: a streamed
`writePartition` session (envelope chunk + body chunks, last one `FLAG_FINAL`,
progress reported device-side on the reply), pull OTA from URL, and partition
download. App partitions go through `esp_ota_*` (image validation, running slot
refused); data partitions are raw erase+write. The built frontend is gzipped into
`www/` and flashed as a FAT partition, updatable independently of the app.

> **Bench-testing note:** since `/api/command` is gone, `curl` no longer drives
> commands — use a WS session client (`scratchpad/ws_cmd.py`) that speaks the
> framing above.

### Settings

Settings are typed leaf objects (`lib`-style, [TypedSettings.h](main/Application/SettingsManager/TypedSettings.h)) declared in the manager that owns them and registered at runtime:

```cpp
inline static UInt32Setting port_{ "myfeature.port", "My Feature Port", 1883 };
// in Init():  settings.Register({ &port_ });
uint32_t p = port_.Get();   // NVS value or the typed default
```

`SettingsManager` is the NVS link; the settings UI is generated dynamically from the registered definitions.

### Deliberately out of scope

MQTT and Home Assistant integration were removed 2026-07-06: the gateway owns
smart-home integration, so the thermostat has no MQTT/HA layer. Do not
reintroduce it — resurrect the old managers from git history only if that ever
changes.

## Conventions

- C++17, no exceptions/RTTI-heavy patterns; `snprintf` with `sizeof` bounds, no `strcpy`/`strcat`.
- JSON is generated with `lib/json/JsonWriter.h` and parsed with `JsonReader` (no external JSON lib); `JsonScope.h` provides RAII `JsonObject`/`JsonArray` with auto-close.
- Firmware version derives from the latest uppercase-`V` git tag (`V0.1.0` → `0.1.0`), or from CI-injected `-DSOFTWARE_VERSION_MAJOR/MINOR/PATCH`, in the root CMakeLists.
