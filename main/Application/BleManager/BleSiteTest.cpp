#include "BleSiteTest.h"
#include "CommandManager/CommandManager.h"
#include "JsonScope.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <algorithm>

namespace
{
    // Brings the reply to roughly the size of a real status reply (~200 bytes),
    // so a ping costs the radio what a normal command costs it. Still one chunk.
    constexpr const char* kReplyPadding =
        "0123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789";
}

BleSiteTest::BleSiteTest(ServiceProvider& serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void BleSiteTest::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    slots_ = static_cast<Slot*>(
        heap_caps_malloc(HistorySeconds * sizeof(Slot), MALLOC_CAP_SPIRAM));
    if (slots_ == nullptr)
    {
        ESP_LOGE(TAG, "No memory for the sample history; site test unavailable");
        return;
    }
    Reset();

    serviceProvider_.getCommandManager().Register(this, commands_);

    linkTimer_.Init("bletest_link", pdMS_TO_TICKS(kLinkPollMs));
    linkTimer_.SetHandler([this] { PollLink(); });
    linkTimer_.Start();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

void BleSiteTest::Reset()
{
    bool up = serviceProvider_.getBleManager().GetLinkState() == BleManager::LinkState::Ready;

    LOCK(mutex_);
    if (slots_ == nullptr) return;

    for (uint32_t i = 0; i < HistorySeconds; i++) slots_[i] = Slot{};
    startUs_ = esp_timer_get_time();

    haveSeq_ = false;
    resolvedThrough_ = 0;
    hasRssi_ = false;
    rssi_ = worstRssi_ = 0;
    ok_ = lost_ = lossRun_ = longestLossRun_ = 0;
    lastRttMs_ = minRttMs_ = maxRttMs_ = 0;
    rttSumMs_ = 0;
    rttCount_ = 0;
    wasUp_ = up;
    downSinceUs_ = 0;
    disconnects_ = 0;
    longestOutageMs_ = 0;

    ESP_LOGI(TAG, "Test reset");
}

void BleSiteTest::SetTestMode(bool on)
{
    serviceProvider_.getBleManager().SetFastReconnect(on);
    ESP_LOGI(TAG, "Test mode %s", on ? "on (fast reconnect)" : "off");
}

// ──────────────────────────────────────────────────────────────
// Recording
// ──────────────────────────────────────────────────────────────

uint32_t BleSiteTest::NowSecond() const
{
    return static_cast<uint32_t>((esp_timer_get_time() - startUs_) / 1000000);
}

BleSiteTest::Slot* BleSiteTest::SlotFor(uint32_t second)
{
    Slot& slot = slots_[second % HistorySeconds];
    if (slot.second != second)
    {
        slot = Slot{};
        slot.second = second;
    }
    return &slot;
}

const BleSiteTest::Slot* BleSiteTest::FindSlot(uint32_t second) const
{
    const Slot& slot = slots_[second % HistorySeconds];
    return slot.second == second ? &slot : nullptr;
}

void BleSiteTest::OnPing(uint32_t seq, uint32_t history, uint32_t lastRttMs)
{
    // Read here rather than on a timer: this runs on the BLE dispatch task, which
    // has the stack for an HCI round trip. A second with no ping has no RSSI, which
    // is also the honest answer — nothing was getting through.
    int8_t rssi = 0;
    bool gotRssi = serviceProvider_.getBleManager().ReadRssi(rssi);

    LOCK(mutex_);
    if (slots_ == nullptr || seq == 0) return;

    uint32_t now = NowSecond();
    bool restart = !haveSeq_ || seq <= resolvedThrough_;

    // A ping's second comes from its sequence number, not from when it arrived.
    // The gateway sends once a second, but arrival jitters around our second
    // boundaries: two pings would share a second and leave a hole in the next.
    // Re-anchored only when the two clocks have drifted visibly apart. Anchored
    // one second back, so a ping arriving just across a boundary still maps to
    // a second that has begun — not to the next one, which would drop every
    // other RSSI sample and leave the line with no two neighbours to join.
    int64_t drift = static_cast<int64_t>(seq) + seqToSecond_ - static_cast<int64_t>(now);
    if (restart || drift > kReanchorSeconds || drift < -kReanchorSeconds)
        seqToSecond_ = static_cast<int64_t>(now) - 1 - static_cast<int64_t>(seq);

    if (gotRssi)
    {
        uint32_t second = SecondOfSeq(seq, now);
        if (second != UINT32_MAX)
        {
            Slot* slot = SlotFor(second);
            slot->hasRssi = true;
            slot->rssi = rssi;
        }
        if (!hasRssi_ || rssi < worstRssi_) worstRssi_ = rssi;
        rssi_ = rssi;
        hasRssi_ = true;
    }

    // The first ping after a reset, or a gateway that restarted its count: what
    // came before it is not ours to judge.
    if (restart)
    {
        haveSeq_ = true;
        resolvedThrough_ = seq - 1;
        return;
    }

    // Settle every ping since the last one we heard. The gateway's history covers
    // the last 32; anything older can only have been sent into an outage.
    for (uint32_t s = resolvedThrough_ + 1; s < seq; s++)
    {
        uint32_t age = seq - 1 - s;                      // 0 = the ping before this one
        bool ok = age < kHistoryBits && ((history >> age) & 1u) != 0;
        uint32_t rttMs = (age == 0) ? lastRttMs : 0;     // only the latest RTT is sent
        RecordOutcome(SecondOfSeq(s, now), ok, rttMs);
    }
    resolvedThrough_ = seq - 1;
}

// The graph second a ping belongs to, or UINT32_MAX when that is before the
// test or older than the kept history. Drift that has not yet triggered a
// re-anchor can point past `now`; that sample lands in the current second
// rather than being lost.
uint32_t BleSiteTest::SecondOfSeq(uint32_t seq, uint32_t now) const
{
    int64_t second = std::min<int64_t>(static_cast<int64_t>(seq) + seqToSecond_, now);
    if (second < 0 || now - second >= HistorySeconds) return UINT32_MAX;
    return static_cast<uint32_t>(second);
}

// `second` is UINT32_MAX for an outcome too old for the graph; it still counts.
void BleSiteTest::RecordOutcome(uint32_t second, bool ok, uint32_t rttMs)
{
    Slot* slot = (second != UINT32_MAX) ? SlotFor(second) : nullptr;

    if (!ok)
    {
        lost_++;
        lossRun_++;
        longestLossRun_ = std::max(longestLossRun_, lossRun_);
        if (slot != nullptr && slot->lost < UINT8_MAX) slot->lost++;
        return;
    }

    ok_++;
    lossRun_ = 0;
    if (slot != nullptr && slot->ok < UINT8_MAX) slot->ok++;

    if (rttMs == 0) return;      // succeeded, but its round trip was not reported

    lastRttMs_ = rttMs;
    minRttMs_ = (rttCount_ == 0) ? rttMs : std::min(minRttMs_, rttMs);
    maxRttMs_ = std::max(maxRttMs_, rttMs);
    rttSumMs_ += rttMs;
    rttCount_++;

    if (slot != nullptr && slot->rttCount < UINT8_MAX)
    {
        slot->rttSumMs += rttMs;
        slot->rttMaxMs = static_cast<uint16_t>(std::max<uint32_t>(slot->rttMaxMs,
                                                                   std::min<uint32_t>(rttMs, UINT16_MAX)));
        slot->rttCount++;
    }
}

void BleSiteTest::PollLink()
{
    BleManager::LinkState state = serviceProvider_.getBleManager().GetLinkState();
    bool up = state == BleManager::LinkState::Ready;
    int64_t nowUs = esp_timer_get_time();

    LOCK(mutex_);
    link_ = state;

    if (wasUp_ && !up)
    {
        disconnects_++;
        downSinceUs_ = nowUs;
        ESP_LOGW(TAG, "Link lost (disconnect #%lu)", static_cast<unsigned long>(disconnects_));
    }
    else if (!wasUp_ && up && downSinceUs_ != 0)
    {
        uint32_t outageMs = static_cast<uint32_t>((nowUs - downSinceUs_) / 1000);
        longestOutageMs_ = std::max(longestOutageMs_, outageMs);
        downSinceUs_ = 0;
        ESP_LOGI(TAG, "Link back after %lu ms", static_cast<unsigned long>(outageMs));
    }
    wasUp_ = up;
}

// ──────────────────────────────────────────────────────────────
// Reading
// ──────────────────────────────────────────────────────────────

BleSiteTest::Summary BleSiteTest::GetSummary() const
{
    Summary s;
    LOCK(mutex_);
    if (slots_ == nullptr) return s;

    s.link = link_;
    s.testSeconds = NowSecond();
    s.hasRssi = hasRssi_;
    s.rssi = rssi_;
    s.worstRssi = worstRssi_;
    s.ok = ok_;
    s.lost = lost_;
    s.longestLossRun = longestLossRun_;
    s.lastRttMs = lastRttMs_;
    s.minRttMs = minRttMs_;
    s.avgRttMs = rttCount_ > 0 ? static_cast<uint32_t>(rttSumMs_ / rttCount_) : 0;
    s.maxRttMs = maxRttMs_;
    s.disconnects = disconnects_;
    s.longestOutageMs = longestOutageMs_;
    if (downSinceUs_ != 0)
        s.currentOutageMs = static_cast<uint32_t>((esp_timer_get_time() - downSinceUs_) / 1000);
    return s;
}

void BleSiteTest::GetBuckets(Bucket* out, uint32_t count, uint32_t secondsPerBucket) const
{
    LOCK(mutex_);
    uint32_t now = (slots_ != nullptr) ? NowSecond() : 0;

    // Bucket edges sit on whole multiples of the bucket size, counted from the
    // test start. Measured back from `now` instead, every second regrouped every
    // column and the whole graph jumped on each refresh.
    //
    // Only complete buckets are shown: a still-filling one draws as a short bar
    // that grows and resets, which reads as loss. At one second per bucket that
    // costs nothing — the newest second has no settled outcome yet anyway.
    uint32_t currentBucketStart = now - now % secondsPerBucket;
    if (currentBucketStart < secondsPerBucket)
    {
        for (uint32_t i = 0; i < count; i++) out[i] = Bucket{};
        return;
    }
    uint32_t lastBucketStart = currentBucketStart - secondsPerBucket;

    for (uint32_t i = 0; i < count; i++)
    {
        Bucket b;
        int32_t  rssiSum = 0;
        uint32_t rssiCount = 0;
        uint32_t rttSum = 0;
        uint32_t rttCount = 0;

        uint32_t bucketsBack = count - 1 - i;
        // A bucket from before the test, or one reaching past the kept history,
        // stays empty rather than drawing as a partly filled column.
        bool beforeTestStart = bucketsBack * secondsPerBucket > lastBucketStart;
        uint32_t start = beforeTestStart ? 0 : lastBucketStart - bucketsBack * secondsPerBucket;
        bool fullyKept = !beforeTestStart && now - start < HistorySeconds;

        for (uint32_t k = 0; k < secondsPerBucket && slots_ != nullptr && fullyKept; k++)
        {
            const Slot* slot = FindSlot(start + k);
            if (slot == nullptr) continue;

            b.ok += slot->ok;
            b.lost += slot->lost;
            if (slot->hasRssi)
            {
                b.rssiMin = (rssiCount == 0) ? slot->rssi : std::min(b.rssiMin, slot->rssi);
                rssiSum += slot->rssi;
                rssiCount++;
            }
            if (slot->rttCount > 0)
            {
                b.rttMaxMs = std::max<uint32_t>(b.rttMaxMs, slot->rttMaxMs);
                rttSum += slot->rttSumMs;
                rttCount += slot->rttCount;
            }
        }

        b.hasRssi = rssiCount > 0;
        if (b.hasRssi) b.rssiAvg = static_cast<int8_t>(rssiSum / static_cast<int32_t>(rssiCount));
        b.hasRtt = rttCount > 0;
        if (b.hasRtt) b.rttAvgMs = rttSum / rttCount;
        out[i] = b;
    }
}

// ──────────────────────────────────────────────────────────────
// Command
// ──────────────────────────────────────────────────────────────

RequestError BleSiteTest::Cmd_Ping(CommandContext& ctx)
{
    uint32_t seq = 0;
    uint32_t history = 0;
    uint32_t rtt = 0;
    RETURN_IF_ERROR(ctx.readArgs(
        Required("seq",  seq),
        Optional("hist", history),
        Optional("rtt",  rtt)
    ));

    OnPing(seq, history, rtt);

    // "seq" must not be the last field: the gateway matches `"seq":N,` so that
    // ping 12 is not mistaken for an echo of ping 123.
    JsonObject root(ctx.out);
    root.field("ok", true);
    root.field("seq", seq);
    root.field("pad", kReplyPadding);
    return RequestError::Ok;
}
