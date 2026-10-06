#pragma once

#include "Screen.h"
#include "ServiceProvider.h"
#include "BleManager/BleSiteTest.h"

// POC: the BLE site test, on one screen. A summary card on top, a live graph
// below it with three views (RSSI, latency, received/lost) and three ranges
// (1, 5, 10 minutes). The data comes from BleSiteTest; this screen only draws.
//
// Being on this screen is what "test mode" means: it turns fast reconnect on
// when shown and off again on the way out, and the shell leaves it up when
// untouched instead of dropping back to home.
class BleTestScreen final : public Screen
{
    static constexpr uint32_t kRefreshMs = 1000;
    // Graph points, whatever the range: one second each at 1 min, ten at 10 min.
    static constexpr uint32_t kColumns = 60;
    static constexpr int      kYLabels = 5;   // one per horizontal grid line

public:
    BleTestScreen(ServiceProvider& serviceProvider, Navigator& navigator)
        : serviceProvider_(serviceProvider), navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;

private:
    enum class View : uint8_t { Rssi, Latency, ReceivedLost };

    // One column of the summary card: title, big value, two small lines.
    struct StatColumn
    {
        lv_obj_t* value = nullptr;
        lv_obj_t* line1 = nullptr;
        lv_obj_t* line2 = nullptr;
    };

    StatColumn AddStatColumn(lv_obj_t* card, int index, const char* title);
    lv_obj_t*  AddSegmented(lv_obj_t* parent, const char* const* map, uint32_t selected,
                            lv_event_cb_t onChange);
    void ShowRangeLabel();

    void Refresh();
    void RefreshSummary();
    void RefreshGraph();
    void ApplyView();
    void SetYLabels(int32_t bottom, int32_t top, bool onlyEnds);

    static void RefreshTimerCb(lv_timer_t* t);
    static void BackCb(lv_event_t* e);
    static void ResetCb(lv_event_t* e);
    static void ViewCb(lv_event_t* e);
    static void RangeCb(lv_event_t* e);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;

    View     view_ = View::Rssi;
    uint32_t secondsPerColumn_ = 1;

    lv_obj_t* stateLabel_ = nullptr;
    StatColumn rssi_;
    StatColumn success_;
    StatColumn latency_;
    StatColumn time_;

    // Two series, drawn back to front. As lines: the extreme (worst RSSI, max
    // latency) behind the average. Stacked: received at the bottom, lost on top.
    lv_obj_t*          chart_ = nullptr;
    lv_chart_series_t* backSeries_ = nullptr;
    lv_chart_series_t* frontSeries_ = nullptr;
    lv_obj_t*          yLabels_[kYLabels] = {};
    lv_obj_t*          rangeLabel_ = nullptr;
    lv_obj_t*          legendLabel_ = nullptr;

    // kColumns of them, in PSRAM: this object lives in internal DRAM, which the
    // BLE stack needs more than a graph does.
    BleSiteTest::Bucket* buckets_ = nullptr;
    lv_timer_t*          refreshTimer_ = nullptr;
};
