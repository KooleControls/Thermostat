#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include "host/ble_hs.h"
#include <cstdint>
#include <cstddef>

class Stream;
class JsonObject;

// ──────────────────────────────────────────────────────────────
// The BLE link to a KC1245 Gateway.
//
// We are the **central**: the gateway advertises its name and gateway id, we
// scan, the installer picks one from a list, and we connect. Access is gated by
// pairing with the gateway's 6 public install-code digits as the BLE passkey —
// there is no login handshake over BLE, because authentication belongs to the
// transport (docs/reasoning/2026-07-29-11h29-...). Nothing is dispatched into
// CommandManager unless the connection is encrypted *and* authenticated.
//
// This step brings the stack up and scans. Connect/pair and the session
// transport (BleSessionLink + dispatch task) land next — see
// docs/backlog/2026-07-29-ble-gateway-link.md.
// ──────────────────────────────────────────────────────────────
class BleManager
{
    static constexpr const char* TAG = "BleManager";

public:
    static constexpr int    MaxPeers   = 8;
    static constexpr size_t MaxNameLen = 30;    // a scan response holds ~29 chars
    static constexpr int32_t ScanDurationMs = 8000;

    // One gateway we have heard. Filled from two packets: the advertisement
    // (gateway id) and the scan response (name), merged by address.
    struct Peer
    {
        ble_addr_t addr{};
        char       name[MaxNameLen + 1] = {};
        uint32_t   dgid = 0;
        bool       hasDgid = false;
        int8_t     rssi = 0;
        bool       used = false;
    };

    explicit BleManager(ServiceProvider& serviceProvider);

    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;
    BleManager(BleManager&&) = delete;
    BleManager& operator=(BleManager&&) = delete;

    void Init();

    /// Kick a scan. False when the stack is down or a scan is already running.
    bool StartScan();
    bool Scanning() const;

    /// Copy out what the current/last scan has heard. Returns the count.
    int GetPeers(Peer* out, int max) const;

    /// "aa:bb:cc:dd:ee:ff" — `out` must hold 18 bytes. Printed MSB-first, which
    /// is the reverse of the on-air byte order.
    static void FormatAddr(const ble_addr_t& addr, char* out, size_t cap);

private:
    void StartStack();
    void OnSync();                                     // controller ready
    int  OnGapEvent(struct ble_gap_event* event);
    void NotePeer(const struct ble_gap_disc_desc& disc);

    static void HostTask(void* param);
    static int  GapEventTrampoline(struct ble_gap_event* event, void* arg);
    static void OnSyncTrampoline();
    static void OnResetTrampoline(int reason);

    void Cmd_BleScan(Stream& in, Stream& out);
    void Cmd_BleStatus(Stream& in, Stream& out);
    void WritePeers(JsonObject& root);

    inline static CommandEntry commands_[] = {
        { "bleScan",   &InvokeCommand<&BleManager::Cmd_BleScan> },
        { "bleStatus", &InvokeCommand<&BleManager::Cmd_BleStatus> },
    };

    inline static BoolSetting enableSetting_{ "ble.enable", "BLE Enable", true };

    ServiceProvider& serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;

    bool    stackUp_ = false;      // NimBLE host running
    bool    synced_ = false;       // controller synced, own address known
    bool    scanning_ = false;
    uint8_t ownAddrType_ = 0;
    Peer    peers_[MaxPeers];
};
