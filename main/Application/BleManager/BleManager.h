#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include "host/ble_hs.h"
#include "esp_timer.h"
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
// transport (docs/reasoning/2026-07-29-11h29-...). Nothing will be dispatched
// into CommandManager unless the connection is encrypted *and* authenticated,
// which is checked before the link is declared usable.
//
// Bring-up order after a connect: encrypt/pair first, then discover, so no
// service discovery ever happens on a plaintext link.
// ──────────────────────────────────────────────────────────────
class BleManager
{
    static constexpr const char* TAG = "BleManager";

public:
    static constexpr int    MaxPeers   = 8;
    static constexpr size_t MaxNameLen = 30;    // a scan response holds ~29 chars
    static constexpr int32_t ScanDurationMs = 8000;
    static constexpr int32_t ConnectTimeoutMs = 10000;
    static constexpr int64_t ReconnectDelayUs = 5LL * 1000 * 1000;

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

    // Down → Connecting → Securing → Discovering → Ready. Only Ready may carry
    // sessions; every other state means "no command surface".
    enum class LinkState : uint8_t { Down, Connecting, Securing, Discovering, Ready };

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

    /// Pair with (or reconnect to) a gateway. `passkey` is the gateway's 6
    /// public install-code digits; it is only needed for the first pairing —
    /// afterwards the stored bond re-encrypts without it. Remembers the peer so
    /// the link comes back by itself after a reboot or a dropout.
    bool Connect(const ble_addr_t& addr, uint32_t passkey);

    /// Drop the link, forget the peer and delete its bond.
    void Forget();

    LinkState GetLinkState() const;

    /// "aa:bb:cc:dd:ee:ff" — `out` must hold 18 bytes. Printed MSB-first, which
    /// is the reverse of the on-air byte order.
    static void FormatAddr(const ble_addr_t& addr, char* out, size_t cap);
    static bool ParseAddr(const char* text, uint8_t type, ble_addr_t& out);

private:
    void StartStack();
    void OnSync();                                     // controller ready
    int  OnGapEvent(struct ble_gap_event* event);
    void NotePeer(const struct ble_gap_disc_desc& disc);

    void StartConnect();                               // uses target_
    void ScheduleReconnect();
    void DropLink(const char* why);
    void StartDiscovery();
    void SetState(LinkState s);

    // GATT discovery chain: service → characteristics → the inbound CCCD →
    // subscribe. Each step is a NimBLE callback, so the state lives in members.
    int OnSvcDisc(const struct ble_gatt_error* error, const struct ble_gatt_svc* svc);
    int OnChrDisc(const struct ble_gatt_error* error, const struct ble_gatt_chr* chr);
    int OnDscDisc(const struct ble_gatt_error* error, const struct ble_gatt_dsc* dsc);
    int OnSubscribed(const struct ble_gatt_error* error);

    static void HostTask(void* param);
    static int  GapEventTrampoline(struct ble_gap_event* event, void* arg);
    static void OnSyncTrampoline();
    static void OnResetTrampoline(int reason);
    static void ReconnectTimerCb(void* arg);
    static int  SvcDiscTrampoline(uint16_t conn, const struct ble_gatt_error* error,
                                  const struct ble_gatt_svc* svc, void* arg);
    static int  ChrDiscTrampoline(uint16_t conn, const struct ble_gatt_error* error,
                                  const struct ble_gatt_chr* chr, void* arg);
    static int  DscDiscTrampoline(uint16_t conn, const struct ble_gatt_error* error,
                                  uint16_t chrValHandle, const struct ble_gatt_dsc* dsc,
                                  void* arg);
    static int  SubscribeTrampoline(uint16_t conn, const struct ble_gatt_error* error,
                                    struct ble_gatt_attr* attr, void* arg);

    void Cmd_BleScan(Stream& in, Stream& out);
    void Cmd_BleStatus(Stream& in, Stream& out);
    void Cmd_BleConnect(Stream& in, Stream& out);
    void Cmd_BleForget(Stream& in, Stream& out);
    void WritePeers(JsonObject& root);
    static const char* StateName(LinkState s);

    inline static CommandEntry commands_[] = {
        { "bleScan",    &InvokeCommand<&BleManager::Cmd_BleScan> },
        { "bleStatus",  &InvokeCommand<&BleManager::Cmd_BleStatus> },
        { "bleConnect", &InvokeCommand<&BleManager::Cmd_BleConnect> },
        { "bleForget",  &InvokeCommand<&BleManager::Cmd_BleForget> },
    };

    inline static BoolSetting   enableSetting_{ "ble.enable", "BLE Enable", true };
    inline static StringSetting peerSetting_{ "ble.peer", "BLE Gateway Address", "" };
    inline static UInt32Setting peerTypeSetting_{ "ble.peerType", "BLE Gateway Address Type", 0 };

    ServiceProvider& serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;

    bool    stackUp_ = false;      // NimBLE host running
    bool    synced_ = false;       // controller synced, own address known
    bool    scanning_ = false;
    uint8_t ownAddrType_ = 0;
    Peer    peers_[MaxPeers];

    LinkState  link_ = LinkState::Down;
    ble_addr_t target_{};
    bool       haveTarget_ = false;
    uint32_t   pendingPasskey_ = 0;      // only set between Connect() and pairing
    uint16_t   connHandle_ = BLE_HS_CONN_HANDLE_NONE;
    uint16_t   mtu_ = 23;                // until the exchange completes
    uint16_t   svcStart_ = 0;
    uint16_t   svcEnd_ = 0;
    uint16_t   inboundValHandle_ = 0;    // gateway notifies here
    uint16_t   outboundValHandle_ = 0;   // we write here
    uint16_t   inboundCccd_ = 0;

    esp_timer_handle_t reconnectTimer_ = nullptr;
};
