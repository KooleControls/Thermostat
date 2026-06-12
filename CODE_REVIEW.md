# Embedded Code Quality Review — Thermostat

**Date:** 2026-04-24
**Codebase:** ESP32 / ESP-IDF 5.0+, FreeRTOS, C++17
**Scope:** 15 .cpp source files + 43 headers, ~5,100 LOC

---

## Overall Score: 80%

---

## Scores by Category

| Category | Score | Notes |
|---|---|---|
| Architecture | 75% | ServiceProvider pattern well applied; MqttManager has divergent change risk (protocol + commands + discovery combined); Settings system requires shotgun surgery on every extension |
| Class Design | 72% | Copy/move correctly deleted in singletons; WiFi/MQTT config stored as raw `char[]` fields instead of config structs (primitive obsession + data clumps); `Task.h` exposes public member variables |
| FreeRTOS / RTOS Safety | 88% | Excellent wrapper classes (Task, Mutex, Semaphore, Timer); ISR-safe variants used correctly (`xTaskNotifyFromISR`); queue-based logging; no heap allocation in ISR context |
| Memory Safety | 90% | Minimal dynamic allocation; all `new`/`malloc` results checked; PSRAM-first strategy in ConsoleManager; no leaks detected |
| String/Buffer Handling | 90% | No `strcpy`/`strcat` found anywhere; all `snprintf` calls use `sizeof` bounds; stack buffers up to 512 bytes but all bounded |
| Error Handling | 83% | Consistent `ESP_ERROR_CHECK` for critical inits; nullptr checks throughout; NVS open failure is a soft failure (non-fatal) — acceptable |
| File Size / Module | 75% | No god files; MqttManager.cpp (470 LOC) is borderline; `PublishDiscovery()` is 112 lines with a repeated JSON pattern |
| Configuration / Settings | 70% | Typed setting keys via `SettingsDefs.h` is a strength; shotgun surgery when adding settings; magic numbers for timeouts and buffer sizes; config fields not grouped into structs |
| Logging | 85% | Queue-based logging with PSRAM circular buffer; WebSocket broadcast present; structured storage; production-ready |
| Crypto / Security | 70% | OTA present via `UpdateManager`; MQTT credentials in use; no explicit TLS configuration or certificate pinning observed in source |
| Hardware Abstraction | 80% | RTOS fully abstracted; `WiFiInterface` properly isolates WiFi events; direct `esp_get_free_heap_size()` call in `MqttManager` breaks the abstraction layer |

---

## Code Smells Found

### Bloaters

| Smell | Location | Description |
|---|---|---|
| Long Method | `MqttManager.cpp` — `PublishDiscovery()` ~112 lines | JSON generation for sensor and button blocks repeated four times; four identical `buf[512]` + JsonWriter blocks in one method |
| Long Method | `CommandManager.cpp` — `Cmd_Partitions()` ~80 lines | Partition enumeration with nested conditional logic |
| Long Method | `WiFiInterface.cpp` — `OnWifiEvent()` ~75 lines | Large switch over WiFi and IP events with nested logic |
| Large Class | `MqttManager.h` | Combines MQTT lifecycle, command dispatch, discovery publishing, event handling, and status management |
| Data Clump | `NetworkManager.h:41-42` | `staSsid_[32]` and `staPassword_[65]` always appear together — should be `struct WiFiCredentials` |
| Data Clump | `MqttManager.cpp:113-118` | `broker`, `user`, `pass`, `port` always fetched as a unit — `struct MqttBrokerConfig` is missing |
| Primitive Obsession | `NetworkManager.h`, `MqttManager.h` | Raw `char[]` fields for configuration groups instead of typed structs |
| Long Parameter List | `WiFiInterface.cpp:58` `StartAP()` | 4 parameters: `ssid, password, channel, maxConnections` — candidate for a parameter object |

### Object-Orientation Abusers

No significant violations. All switch statements are enum-based and compact. No refused bequest or temporary fields found.

### Change Preventers

| Smell | Location | Description |
|---|---|---|
| Divergent Change | `MqttManager.cpp` | Changes for: new MQTT event, new command handler, new discovery entity, settings modifications |
| Shotgun Surgery | `SettingsDefs.h` + `SettingsManager.cpp` + all consumers | Adding a setting requires changes in defs, the `ApplyDefaults()` switch, and every read site |
| Shotgun Surgery | `NetworkManager.h`, `WiFiInterface.cpp`, `SettingsDefs.h` | A WiFi config change touches at least 4 files |

### Dispensables

| Smell | Location | Description |
|---|---|---|
| Duplicate Code | `MqttManager.cpp:~285,344,398,440` | `char buf[512]; BufferStream stream(...); JsonWriter json(...); json.beginObject();` pattern repeated 4 times |
| Duplicate Code | `MqttManager.cpp` | `snprintf(topic, sizeof(topic), "%s/...", baseTopic_)` topic construction repeated 15+ times; no helper exists |
| Dead Code | `Task.h:8-11` | Public member variables that should be private |

### Couplers

| Smell | Location | Description |
|---|---|---|
| Feature Envy | `MqttManager.cpp` — `PublishEntityDiscovery()` | Builds Home Assistant JSON directly instead of delegating to an HA-specific class |
| Inappropriate Intimacy | `MqttManager.cpp:283` | Direct `esp_get_free_heap_size()` call — bypasses `DeviceManager` abstraction |
| Inappropriate Intimacy | `WebServerManager.cpp:77-80` | Raw `s_instance_` pointer in lambda for ESP-IDF C-callback boundary |

---

## Issues

### Critical — Fix Before Ship

None found. The codebase is production-ready from a safety and correctness perspective.

### Medium — Technical Debt

| Location | Description | Recommended Fix |
|---|---|---|
| `MqttManager.cpp` — `PublishDiscovery()` | 112 lines with 4× repeated JSON+publish pattern | Extract `PublishEntityBlock(component, objectId, writeFields)` helper; reduce to ~60 lines |
| `MqttManager.h` | Class has 5+ unrelated responsibilities | Extract `MqttDiscoveryPublisher` for HA discovery; leave MqttManager for protocol + routing only |
| `NetworkManager.h:41-42`, `MqttManager.cpp:113` | Data clumps: WiFi and MQTT config as separate char fields | Introduce `struct WiFiCredentials` and `struct MqttBrokerConfig` |
| `SettingsDefs.h` + `SettingsManager.cpp` | Shotgun surgery on every settings addition | Consider a data-driven defaults table instead of a switch in `ApplyDefaults()` |

### Low — Code Quality

| Location | Description |
|---|---|
| `MqttManager.h:61-62`, `NetworkManager.h:13-14` | Magic numbers for max handlers, timeouts, retries — replace with named `constexpr` |
| `Task.h:8-11` | Public member variables — make private with accessors or remove |
| `MqttManager.cpp` | Topic construction `snprintf(topic, sizeof(topic), "%s/...", baseTopic_)` repeated 15+ times — extract `BuildTopic(suffix)` helper |
| `WebServerManager.cpp:77-80` | `s_instance_` raw pointer in lambda — document why it is needed (C-callback boundary) |

---

## What the Smells Tell You Collectively

The technical debt is concentrated in `MqttManager`: it is the only class that combines MQTT protocol, application logic, and Home Assistant discovery, causing both divergent change and duplicate JSON patterns. The rest of the codebase is consistent and manageable. The highest-ROI fix is extracting discovery generation out of `MqttManager` — that simultaneously resolves the feature envy, the 112-line method, and the duplicate code.

---

## What's Done Well

- **Memory-conscious design**: PSRAM-first allocation, minimal heap use, zero `strcpy`/`strcat`
- **FreeRTOS abstraction**: consistent C++ wrappers for Task, Mutex, Semaphore, Timer with correct ISR-safe variants
- **Error handling**: `ESP_ERROR_CHECK` applied consistently; nullptr checks present throughout
- **Initialization order**: explicit and correctly sequenced in `main.cpp` (Settings before consumers)
- **No TODOs or incomplete sections**: code is complete and production-ready
- **Queue-based logging**: asynchronous logging via FreeRTOS queue with WebSocket broadcast is the correct pattern
- **ServiceProvider pattern**: clean dependency injection without tight coupling between managers

---

## Architecture Overview

```
main.cpp
  └── ApplicationContext (g_appContext)
        ├── ConsoleManager        ← PSRAM queue-based log capture + WS broadcast
        ├── SettingsManager       ← NVS-backed typed key/value store
        ├── NetworkManager
        │     ├── WiFiInterface   ← STA/AP event handling
        │     └── DnsConfiguration ← mDNS
        ├── CommandManager        ← CLI + MQTT command dispatch
        ├── MqttManager           ← MQTT protocol + HA discovery  [DIVERGENT CHANGE RISK]
        ├── DeviceManager         ← Device state (LED, reboot, etc.)
        ├── HomeAssistantManager  ← HA integration (thin wrapper)
        ├── UpdateManager         ← OTA firmware update
        └── WebServerManager
              └── WebSocketHandler ← WS log/command relay

lib/
  ├── rtos/    Task, Mutex, RecursiveMutex, Semaphore, Timer
  ├── json/    JsonWriter, JsonHelpers (zero-heap JSON)
  └── system/  DateTime, TimeSpan
```

---

## Largest Files

| File | LOC | Score | Notes |
|---|---|---|---|
| `MqttManager.cpp` | 470 | 65% | Too many responsibilities; duplicate JSON patterns |
| `WebServerManager.cpp` | 307 | 78% | Upload/download/mount combined; acceptable |
| `CommandManager.cpp` | 291 | 80% | `Cmd_Partitions()` is long but readable |
| `SettingsManager.cpp` | 242 | 75% | `ApplyDefaults()` switch grows with every setting |
| `WiFiInterface.cpp` | 235 | 80% | `OnWifiEvent()` complex but logically grouped |
| `DateTime.cpp` | 194 | 85% | Library code; acceptable |
| `NetworkManager.cpp` | 183 | 82% | Clean |
| `UpdateManager.cpp` | 176 | 83% | OTA flow well structured |
| `WebSocketHandler.cpp` | 169 | 85% | Single responsibility |
| `ConsoleManager.cpp` | 158 | 88% | Strong design |

---

## Priority Fix List

1. **Extract `BuildTopic(suffix)` helper in MqttManager** — removes 15+ repeated `snprintf` topic calls in one step; lowest effort, highest readability ROI
2. **Refactor `PublishDiscovery()` into per-entity helpers** — reduces from 112 to ~40 lines; eliminates duplicate JSON blocks
3. **Introduce `struct MqttBrokerConfig` and `struct WiFiCredentials`** — eliminates data clumps and primitive obsession in NetworkManager and MqttManager
4. **Extract `MqttDiscoveryPublisher` class** — resolves divergent change in MqttManager; HA discovery becomes independent of MQTT protocol
5. **Replace magic numbers with named `constexpr`** — `kMqttMaxHandlers`, `kWifiConnectTimeoutMs`, `kMaxConsoleLines`, etc.
6. **Data-driven `ApplyDefaults()`** — replace switch with a table of `{key, default_value}` pairs to prevent shotgun surgery on settings additions
7. **Privatize member variables in `Task.h`** — quick win, improves encapsulation
8. **Document `s_instance_` pattern in WebServerManager** — or replace with a callback wrapper that does not require a raw singleton pointer

---

*Generated by Claude Code — claude-sonnet-4-6*
