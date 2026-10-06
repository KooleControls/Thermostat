#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Timer.h"
#include "BleManager.h"
#include "CommandManager/CommandEntry.h"
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// POC: BLE site test — the thermostat half.
//
// Carry the thermostat around a house and see, live, whether the link to the
// gateway holds. The gateway sends `bletest ping` once a second; each ping
// carries the outcome of the ones before it (see the gateway's BleSiteTest), so
// the round-trip result of every sequence number ends up here, including the
// ones that never arrived because the link was down. This class keeps the
// bookkeeping; BleTestScreen shows it.
//
// Every ping is bookkept, whichever screen is up. "Test mode" only switches the
// BLE manager to an immediate reconnect, so an outage measures the radio and the
// stack instead of the production back-off.
// ──────────────────────────────────────────────────────────────
class BleSiteTest
{
    static constexpr const char* TAG = "BleSiteTest";

    // How often the link state is sampled for disconnects and outage lengths.
    static constexpr uint32_t kLinkPollMs = 100;
    // The gateway reports outcomes for the last 32 pings (one u32 of history).
    static constexpr uint32_t kHistoryBits = 32;

public:
    /// Raw one-second samples kept: the longest graph range, 10 minutes.
    static constexpr uint32_t HistorySeconds = 600;

    struct Summary
    {
        BleManager::LinkState link = BleManager::LinkState::Down;
        uint32_t testSeconds = 0;

        bool     hasRssi = false;
        int8_t   rssi = 0;            // latest
        int8_t   worstRssi = 0;

        uint32_t ok = 0;
        uint32_t lost = 0;
        uint32_t longestLossRun = 0;  // consecutive lost pings

        uint32_t lastRttMs = 0;       // 0 = none yet
        uint32_t minRttMs = 0;
        uint32_t avgRttMs = 0;
        uint32_t maxRttMs = 0;

        uint32_t disconnects = 0;
        uint32_t longestOutageMs = 0;
        uint32_t currentOutageMs = 0; // 0 while the link is up
    };

    /// One graph column: the raw seconds it spans, summarised. Average and
    /// extreme are both kept so a spike stays visible next to the norm.
    struct Bucket
    {
        bool     hasRssi = false;
        int8_t   rssiAvg = 0;
        int8_t   rssiMin = 0;

        bool     hasRtt = false;
        uint32_t rttAvgMs = 0;
        uint32_t rttMaxMs = 0;

        uint32_t ok = 0;
        uint32_t lost = 0;
    };

    explicit BleSiteTest(ServiceProvider& serviceProvider);

    BleSiteTest(const BleSiteTest&) = delete;
    BleSiteTest& operator=(const BleSiteTest&) = delete;
    BleSiteTest(BleSiteTest&&) = delete;
    BleSiteTest& operator=(BleSiteTest&&) = delete;

    void Init();

    /// Start over: clears every counter and the history, restarts the clock.
    void Reset();

    void SetTestMode(bool on);

    Summary GetSummary() const;

    /// Fills `count` buckets of `secondsPerBucket` each, oldest first; the last
    /// one ends at the current second.
    void GetBuckets(Bucket* out, uint32_t count, uint32_t secondsPerBucket) const;

private:
    // One second of raw data. `second` tags which second the slot holds, so a
    // ring slot left over from ten minutes ago reads as empty without a sweep.
    struct Slot
    {
        uint32_t second = UINT32_MAX;
        uint32_t rttSumMs = 0;
        uint16_t rttMaxMs = 0;
        uint8_t  rttCount = 0;
        uint8_t  ok = 0;
        uint8_t  lost = 0;
        bool     hasRssi = false;
        int8_t   rssi = 0;
    };

    uint32_t NowSecond() const;
    Slot*    SlotFor(uint32_t second);
    const Slot* FindSlot(uint32_t second) const;

    void OnPing(uint32_t seq, uint32_t history, uint32_t lastRttMs);
    void RecordOutcome(uint32_t second, bool ok, uint32_t rttMs);
    void PollLink();

    RequestError Cmd_Ping(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "bletest", "ping", &InvokeCommand<&BleSiteTest::Cmd_Ping> },
    };

    ServiceProvider& serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Timer linkTimer_;

    Slot* slots_ = nullptr;          // HistorySeconds of them, in PSRAM

    int64_t  startUs_ = 0;

    bool     haveSeq_ = false;
    uint32_t resolvedThrough_ = 0;   // highest seq whose outcome is counted

    bool     hasRssi_ = false;
    int8_t   rssi_ = 0;
    int8_t   worstRssi_ = 0;

    uint32_t ok_ = 0;
    uint32_t lost_ = 0;
    uint32_t lossRun_ = 0;
    uint32_t longestLossRun_ = 0;

    uint32_t lastRttMs_ = 0;
    uint32_t minRttMs_ = 0;
    uint32_t maxRttMs_ = 0;
    uint64_t rttSumMs_ = 0;
    uint32_t rttCount_ = 0;

    BleManager::LinkState link_ = BleManager::LinkState::Down;
    bool     wasUp_ = false;
    int64_t  downSinceUs_ = 0;       // 0 = not in an outage we are timing
    uint32_t disconnects_ = 0;
    uint32_t longestOutageMs_ = 0;
};
