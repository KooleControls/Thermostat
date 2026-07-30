#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include "BleSessionLink.h"
#include "SessionMux.h"
#include "Task.h"
#include "host/ble_hs.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <atomic>
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
class BleManager : public SessionMux::Sink
{
    static constexpr const char* TAG = "BleManager";

public:
    static constexpr int    MaxPeers   = 8;
    static constexpr size_t MaxNameLen = 30;    // a scan response holds ~29 chars
    static constexpr int32_t ScanDurationMs = 8000;
    static constexpr int32_t ConnectTimeoutMs = 10000;
    static constexpr int64_t ReconnectDelayUs = 5LL * 1000 * 1000;

    // Below this much free internal DRAM the stack is not started at all: the
    // controller needs tens of KB of it and the host task a 4 KB stack, and both
    // failure modes are bad (a controller assert boot-loops the panel; a failed
    // host task is silent). Measured floor, not a guess — see sdkconfig.defaults.
    static constexpr size_t kMinInternalHeap = 48 * 1024;

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

    /// SessionMux::Sink — a request has arrived over BLE and is ready to run.
    /// Called on the dispatch task, never on the NimBLE host task.
    void OnSessionOpened(Session& session) override;

    /// Kick a scan. False when the stack is down or a scan is already running.
    bool StartScan();
    bool Scanning() const;

    /// Copy out what the current/last scan has heard. Returns the count.
    int GetPeers(Peer* out, int max) const;

    /// Pair with (or reconnect to) a gateway. `code` is the gateway's 6 public
    /// install-code digits as TEXT — empty means "no code, rely on the stored
    /// bond". Text, not a number, because "000000" is a perfectly valid passkey
    /// and a numeric 0 cannot be told apart from "none given". Remembers the peer
    /// so the link comes back by itself after a reboot or a dropout.
    bool Connect(const ble_addr_t& addr, const char* code);

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

    void EnqueueChunk(const struct os_mbuf* om);
    void DispatchLoop();

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
    uint32_t   pendingPasskey_ = 0;      // only meaningful while havePasskey_
    bool       havePasskey_ = false;     // set between Connect() and pairing
    uint16_t   connHandle_ = BLE_HS_CONN_HANDLE_NONE;
    uint16_t   mtu_ = 23;                // until the exchange completes
    uint16_t   svcStart_ = 0;
    uint16_t   svcEnd_ = 0;
    uint16_t   inboundValHandle_ = 0;    // gateway notifies here
    uint16_t   outboundValHandle_ = 0;   // we write here
    uint16_t   inboundCccd_ = 0;

    esp_timer_handle_t reconnectTimer_ = nullptr;

    // ── Session transport ───────────────────────────────────────
    // Inbound notifications are queued by the NimBLE callback and drained by a
    // task of our own, because Session::read() blocks waiting for the next chunk
    // and blocking the host task would deadlock the radio.
    //
    // Depth is set by the worst stall the consumer can take while chunks keep
    // arriving, not by the request/reply traffic it was first written for. A
    // firmware push streams continuously and the consumer pauses to write flash —
    // measured at up to 20 ms, several chunks' worth — so a depth of 4 overflowed
    // and dropped chunks, silently corrupting the image. 16 costs ~3 KB more of
    // internal DRAM and leaves margin over the observed worst case.
    static constexpr int    kInQueueDepth = 16;
    // Command handlers run on this task, so it carries whatever the deepest handler
    // needs. 4 KB left only ~1.7 KB headroom on a shallow command, which is too thin
    // a margin for a stack that hosts arbitrary handlers — and too thin to notice
    // before something overruns it.
    static constexpr size_t kDispatchStack = 6144;
    // 100 x 50 ms: long enough to cover discovery after a bonded reconnect.
    static constexpr int    kReadyWaitTicks = 100;

    QueueHandle_t     inQueue_ = nullptr;
    SemaphoreHandle_t writeDone_ = nullptr;   // one outbound write in flight

    // Raised by the host task when a chunk had to be dropped, cleared by the
    // dispatch task when it starts a session. A hole in a stream is invisible to
    // everything downstream, so the link ends the stream instead of letting a
    // consumer write on and fail a hash check minutes later.
    std::atomic<bool> inboundDropped_{false};
    Task              dispatchTask_;

    // Framing windows, deliberately in PSRAM: internal DRAM is the scarce
    // resource here and neither buffer is touched from an ISR or with the cache
    // disabled. Layout is [ 3-byte chunk header | payload ].
    uint8_t* outFrame_ = nullptr;
    uint8_t* inFrame_ = nullptr;
    size_t   outPayloadCap_ = 0;
};
