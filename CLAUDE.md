# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

The **KC Thermostat** — an OpenTherm room thermostat (OT master) that pairs with the KC1245 Gateway as a drop-in replacement for the third-party unit. Built on the Strux template (ESP-IDF v6.0, C++, FreeRTOS, React web UI); Strux is a local git remote (`strux`) and template improvements flow both ways. Backlog: `docs/backlog/` (deliverables tracked in Jira: RA2-395, RA2-437, RA2-435).

Deviations from stock Strux: **no MQTT / Home Assistant managers** (the gateway owns smart-home integration).

**Open-source (approved 2026-07-27).** This repo is public, so: **don't leak existing KC internals** — the connection-server protocol, keys, and fleet semantics live on the *gateway*, and `CommandManager` stays a generic, transport-agnostic command surface.

*Refined 2026-07-29:* the rule is about leaking internals, **not about KC-ness**. New KC-specific code **is allowed when it is confined to the BLE manager**, because a third party can delete that one module, plug in their own transport, and everything else still serves them. The test: *could someone rip out `BleManager` and still have a working generic thermostat?* See `docs/reasoning/2026-07-29-10h55-kc-specific-code-lives-in-the-ble-manager.md`.

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

There are no automated tests; verification is building, flashing, and driving the device over its own wire:

1. `idf.py build`, `idf.py -p <PORT> flash` — find the port, don't trust a number in a doc.
2. Open a WebSocket to `ws://<device>/ws`. **No login step** — auth is three ordinary commands (`auth hello|login|resume`) and is off entirely while `web.password` is empty, which is the default.
3. Send a chunk: `[sid u16 LE][flags u8][payload]`, payload starting with the envelope line `{"type":"<category> <command>", …args}\n`, body after it — same chunk or further chunks sharing the sid. `FLAG_FINAL` (0x01) on the last.
4. Read chunks with your sid until one carries `FLAG_FINAL` (or `FLAG_REJECT` 0x02, payload = reason). **Skip session 0** — those are log broadcasts, not your reply. Non-final chunks are reply data, or progress on a long write.
5. Keep each payload inside the transport's inbound window (4096 on the WebSocket) — a larger frame is refused, not split.

`help list` enumerates every category and command off the device, and `help list -category X -command Y` returns that command's declared arguments, so a probe script needs no source to know what to send.

### Where docs go

- **`docs/backlog/` — concrete software work planned to be done.** One file per topic; the only work-tracking layer in this repo. A resolved item is **deleted**, not left with a DONE banner; what mattered about it lives in a note by then. Deliverables and undecided/maybe-someday items live in Jira (RA2-395, RA2-437, RA2-435), not here — and there is no set-aside "ideas" layer.
- **`docs/reasoning/` — why things are the way they are.** Append-only, immutable once written, one understanding-delta per note, dated. Never edited: a new understanding is a new note, related to the old one via `builds-on` or `supersedes`. This is the durable record — prefer it over prose documentation anywhere.

Upstream Strux removed its design documents, implementation plans and ideas folder on 2026-08-05, for a reason that applies here too: they assert the present tense, so they rot faster than they are read (`docs/reasoning/2026-08-05-15h29-a-document-asserts-the-present-tense-so-it-rots.md`). A plan goes in the backlog; the reasoning behind it goes in a note; how to operate something goes in this file. Deleting a doc is not losing it — git has it.

## Architecture

### Manager pattern (dependency injection)

Everything in firmware is a "manager" owned by `ApplicationContext` ([main/Application/ApplicationContext.h](main/Application/ApplicationContext.h)), which implements the pure-virtual `ServiceProvider` interface ([main/Application/ServiceProvider.h](main/Application/ServiceProvider.h)). Every manager:

- takes `ServiceProvider&` in its constructor and reaches other managers through it (never directly),
- has copy/move deleted,
- initializes in `Init()` guarded by an `InitState` (`lib/rtos/InitState.h`), not in the constructor.

`main.cpp` is only ordered `Init()` calls — order matters (Console → Settings → System → Network → Time → Command → Board → RoomTemperature → OpenTherm → Climate → HotWater → Display → Update → WebServer → Ble). Adding a manager means: create the class, add it to `ServiceProvider`, `ApplicationContext`, `main.cpp`, and `main/CMakeLists.txt` (both `SOURCE_FILES_LIST` and `INCLUDE_DIRS_LIST` — sources are listed explicitly, no globbing).

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

- Handlers have the signature `RequestError Handler(CommandContext& ctx)` and *pull* their arguments: `RETURN_IF_ERROR(ctx.readArgs(Required("path", path), Optional("offset", offset)))` declares them all in one call, and `ctx.in` is then positioned at the body. The handler writes its reply to `ctx.out`. Returning anything but `RequestError::Ok` refuses the request (the protocol layer turns it into a `FLAG_REJECT` with a reason) — a wrong *answer* is still `Ok` with `{"ok":false}`, because form failures and meaning are different things. JSON is a dialect the handler opts into by constructing `JsonObject` on line one; binary payloads (firmware chunks) use the same contract.
- Owners declare an `inline static CommandEntry commands_[]` table ([CommandEntry.h](main/Application/CommandManager/CommandEntry.h)) with `InvokeCommand<&Owner::Method>` trampolines, and hand it to `CommandManager::Register()` from their `Init()`. Tables must have static storage duration — a registered entry that dies aborts with `FATAL`.
- `help list` is the registry describing itself and the one command `CommandManager` owns: categories and names come off the chain, and a command's *arguments* come from the command itself, by re-dispatching it with a `DescribeArgReader` that prints the declarations instead of filling them and stops the handler at its own `RETURN_IF_ERROR`. So calling `ctx.readArgs(...)` is not optional — a handler that skips it has no `help` and, worse, runs its body when described (logged as an error).
- Two transports reach `Execute()`, and they differ *only* below `SessionLink` ([SessionLink.h](main/lib/protocol/SessionLink.h) — the protocol layer lives in `lib/protocol/`, not under a transport, because the transports depend on it and not the reverse): the local browser WebSocket (`WsSessionLink`, frames read on the httpd task) and the BLE link to the gateway (`BleSessionLink`). Above that seam everything is shared — `Session` (the stream), `protocol::RunCommandSession` in [CommandEnvelope.h](main/lib/protocol/CommandEnvelope.h) (names the request, dispatches it, closes or refuses the reply), and `AuthGate` — so no handler knows or cares which transport it is serving. There is no HTTP command route; HTTP serves static files only. Wire format is binary session chunks `[session:u16 LE][flags:u8][payload]`, not a JSON envelope.
- Auth is a property of the *link*, not of the session layer: the browser gets the password/token handshake through `AuthGate`, while BLE proves the peer at the link layer (install-code passkey + bonding) and never routes through it. See `docs/reasoning/2026-07-29-11h29-authentication-belongs-to-the-transport.md`.

Log lines broadcast to authenticated WebSocket clients via `ConsoleManager` on the
reserved broadcast session (0). The frontend side is a singleton `BackendService`
([frontend/src/lib/backend.ts](frontend/src/lib/backend.ts)) that matches replies
to requests by session id and auto-reconnects.

`UpdateManager`'s entire external surface is its command table, and partition
writes are **addressed**: `partition write -partition <label>` streams a whole
image in one command (erase as it goes, activate at the end — what the web UI
sends), while `partition write -partition <label> -offset <n>` writes one piece
at a stated offset and leaves `partition clear` and `partition activate` to the
caller. The offset form is what a sender driving the upload in pieces uses: each
piece is acked on its own, so the sender cannot outrun the device and a lost
piece costs one retry instead of the whole image. Progress on a long write comes
back device-side as non-final `{"p":<bytesWritten>}` chunks — bytes actually in
flash, not bytes queued. App partitions go through `esp_ota_*` (image validation,
running slot refused); data partitions are raw erase+write. The built frontend is
gzipped into `www/` and flashed as a FAT partition, updatable independently of
the app.

> **Bench-testing note:** there is no HTTP command route, so `curl` cannot drive
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

**A key is at most 15 characters** — NVS's limit, asserted in `Register()` at *runtime*, so an over-long key compiles fine and then boot-loops the device on the assert. Nothing catches it earlier. `telemetry.enabled` (17) does not fit; `telem.enabled` does.

### Deliberately out of scope

MQTT and Home Assistant integration were removed 2026-07-06: the gateway owns
smart-home integration, so the thermostat has no MQTT/HA layer. Do not
reintroduce it — resurrect the old managers from git history only if that ever
changes.

## Conventions

- C++17, no exceptions/RTTI-heavy patterns; `snprintf` with `sizeof` bounds, no `strcpy`/`strcat`.
- JSON is generated with `lib/json/JsonWriter.h` and parsed with `JsonReader` (no external JSON lib); `JsonScope.h` provides RAII `JsonObject`/`JsonArray` with auto-close.
- Firmware version derives from the latest uppercase-`V` git tag (`V0.1.0` → `0.1.0`), or from CI-injected `-DSOFTWARE_VERSION_MAJOR/MINOR/PATCH`, in the root CMakeLists.
