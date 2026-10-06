#include "BleTestScreen.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace
{
    // ── Layout (480 x 480) ───────────────────────────────────
    constexpr int32_t kCardY = UiTheme::HeaderH;
    constexpr int32_t kCardH = 116;
    constexpr int32_t kCardPad = 12;
    // Summary columns, widest where the text is longest (latency's min/avg line).
    constexpr int32_t kColumnX[] = { 0, 80, 190, 314 };
    constexpr int32_t kColumnW[] = { 80, 110, 124, 110 };

    constexpr int32_t kControlsY = kCardY + kCardH + 10;
    constexpr int32_t kControlsH = 44;
    constexpr int32_t kViewsW = 290;
    constexpr int32_t kRangesW = 480 - 2 * UiTheme::Pad - kViewsW - 10;

    constexpr int32_t kYLabelW = 40;
    constexpr int32_t kChartX = UiTheme::Pad + kYLabelW + 4;
    constexpr int32_t kChartY = kControlsY + kControlsH + 16;
    constexpr int32_t kChartW = 480 - kChartX - UiTheme::Pad;
    constexpr int32_t kChartH = 194;

    // ── Graph scales ─────────────────────────────────────────
    // Fixed so walks can be compared by eye: below -100 dBm BLE is gone anyway.
    constexpr int32_t kRssiBottom = -110;
    constexpr int32_t kRssiTop = -30;
    // Latency picks the first of these that fits the worst spike in view.
    // Multiples of 4, so every grid line lands on a whole number.
    constexpr uint32_t kLatencyScales[] = { 40, 100, 200, 400, 1000, 2000, 4000 };
    constexpr uint32_t kRangeSecondsPerColumn[] = { 1, 5, 10 };

    const char* const kViewMap[] = { "RSSI", "Latency", "Rx/Lost", "" };
    const char* const kRangeMap[] = { "1m", "5m", "10m", "" };

    uint32_t Rgb(lv_color_t c) { return lv_color_to_u32(c) & 0xFFFFFF; }

    /// Like Screen::SetLabelText, and recolours only when the text changes, so
    /// a steady value costs no redraw.
    void SetColoredText(lv_obj_t* label, const char* text, lv_color_t color)
    {
        if (strcmp(lv_label_get_text(label), text) == 0) return;
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, color, 0);
    }

    uint32_t LatencyScale(uint32_t peakMs)
    {
        for (uint32_t scale : kLatencyScales)
            if (peakMs <= scale) return scale;
        return kLatencyScales[std::size(kLatencyScales) - 1];
    }

    /// "1.2 s" from milliseconds.
    void FormatSeconds(char* out, size_t cap, const char* prefix, uint32_t ms)
    {
        snprintf(out, cap, "%s%" PRIu32 ".%" PRIu32 " s", prefix, ms / 1000, (ms % 1000) / 100);
    }
}

// ──────────────────────────────────────────────────────────────
// Build
// ──────────────────────────────────────────────────────────────

void BleTestScreen::Build(lv_obj_t* root)
{
    // Build runs again on every theme change; the buffer and the timer outlive
    // the widget tree, so they are made once.
    if (buckets_ == nullptr)
        buckets_ = static_cast<BleSiteTest::Bucket*>(
            heap_caps_malloc(kColumns * sizeof(BleSiteTest::Bucket), MALLOC_CAP_SPIRAM));
    if (refreshTimer_ == nullptr)
        refreshTimer_ = lv_timer_create(RefreshTimerCb, kRefreshMs, this);

    lv_obj_t* back = AddHeader(root, "BLE Test", LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back, BackCb, LV_EVENT_CLICKED, this);
    lv_obj_t* header = lv_obj_get_parent(back);

    lv_obj_t* reset = AddIconButton(header, LV_SYMBOL_REFRESH);
    lv_obj_align(reset, LV_ALIGN_RIGHT_MID, -UiTheme::Pad / 2, 0);
    lv_obj_add_event_cb(reset, ResetCb, LV_EVENT_CLICKED, this);

    stateLabel_ = lv_label_create(header);
    lv_obj_set_style_text_font(stateLabel_, &lv_font_montserrat_20, 0);
    lv_label_set_text(stateLabel_, "");
    lv_obj_align(stateLabel_, LV_ALIGN_RIGHT_MID, -(UiTheme::Pad / 2 + UiTheme::IconBtn), 0);

    // ── Summary card ─────────────────────────────────────────
    lv_obj_t* card = lv_obj_create(root);
    lv_obj_set_size(card, 480 - 2 * UiTheme::Pad, kCardH);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, kCardY);
    lv_obj_set_style_bg_color(card, UiTheme::Surface(), 0);
    lv_obj_set_style_radius(card, UiTheme::Radius, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UiTheme::Line(), 0);
    lv_obj_set_style_pad_all(card, kCardPad, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    rssi_ = AddStatColumn(card, 0, "RSSI");
    success_ = AddStatColumn(card, 1, "Success");
    latency_ = AddStatColumn(card, 2, "Latency ms");
    time_ = AddStatColumn(card, 3, "Test time");

    // ── View and range selectors ─────────────────────────────
    // Selections survive a rebuild: they live in members, not in the widgets.
    lv_obj_t* views = AddSegmented(root, kViewMap, static_cast<uint32_t>(view_), ViewCb);
    lv_obj_set_size(views, kViewsW, kControlsH);
    lv_obj_set_pos(views, UiTheme::Pad, kControlsY);

    uint32_t rangeIndex = 0;
    for (uint32_t i = 0; i < std::size(kRangeSecondsPerColumn); i++)
        if (kRangeSecondsPerColumn[i] == secondsPerColumn_) rangeIndex = i;
    lv_obj_t* ranges = AddSegmented(root, kRangeMap, rangeIndex, RangeCb);
    lv_obj_set_size(ranges, kRangesW, kControlsH);
    lv_obj_set_pos(ranges, 480 - UiTheme::Pad - kRangesW, kControlsY);

    // ── Graph ────────────────────────────────────────────────
    chart_ = lv_chart_create(root);
    lv_obj_set_size(chart_, kChartW, kChartH);
    lv_obj_set_pos(chart_, kChartX, kChartY);
    lv_obj_set_style_bg_color(chart_, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_opa(chart_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart_, 0, 0);
    lv_obj_set_style_radius(chart_, 0, 0);
    // No padding, so the grid lines sit exactly where the y labels are put.
    lv_obj_set_style_pad_all(chart_, 0, 0);
    lv_obj_set_style_pad_column(chart_, 2, 0);                  // gap between bars
    lv_obj_set_style_line_color(chart_, UiTheme::Line(), 0);
    lv_obj_set_style_line_width(chart_, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(chart_, 0, LV_PART_INDICATOR);       // no dot per point
    lv_obj_set_style_height(chart_, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(chart_, LV_OBJ_FLAG_CLICKABLE);
    lv_chart_set_point_count(chart_, kColumns);
    lv_chart_set_div_line_count(chart_, kYLabels, 0);
    backSeries_ = lv_chart_add_series(chart_, UiTheme::Warn(), LV_CHART_AXIS_PRIMARY_Y);
    frontSeries_ = lv_chart_add_series(chart_, UiTheme::Good(), LV_CHART_AXIS_PRIMARY_Y);

    for (int i = 0; i < kYLabels; i++)
    {
        lv_obj_t* label = lv_label_create(root);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, UiTheme::TextDim(), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(label, kYLabelW);
        lv_label_set_text(label, "");
        lv_obj_set_pos(label, UiTheme::Pad, kChartY + i * (kChartH - 1) / (kYLabels - 1) - 9);
        yLabels_[i] = label;
    }

    rangeLabel_ = lv_label_create(root);
    lv_obj_set_style_text_font(rangeLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rangeLabel_, UiTheme::TextDim(), 0);
    lv_obj_align_to(rangeLabel_, chart_, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    ShowRangeLabel();

    lv_obj_t* now = lv_label_create(root);
    lv_obj_set_style_text_font(now, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(now, UiTheme::TextDim(), 0);
    lv_label_set_text(now, "now");
    lv_obj_align_to(now, chart_, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 4);

    legendLabel_ = lv_label_create(root);
    lv_obj_set_style_text_font(legendLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(legendLabel_, UiTheme::TextDim(), 0);
    lv_label_set_recolor(legendLabel_, true);
    // Fixed width, centred text: align_to places once, and the text changes per view.
    lv_obj_set_width(legendLabel_, kChartW - 120);
    lv_obj_set_style_text_align(legendLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(legendLabel_, "");
    lv_obj_align_to(legendLabel_, chart_, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
}

BleTestScreen::StatColumn BleTestScreen::AddStatColumn(lv_obj_t* card, int index, const char* title)
{
    const int32_t x = kColumnX[index];
    const int32_t w = kColumnW[index] - 4;

    auto addLabel = [&](const lv_font_t* font, lv_color_t color, int32_t y) {
        lv_obj_t* label = lv_label_create(card);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, color, 0);
        lv_obj_set_width(label, w);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
        lv_label_set_text(label, "");
        lv_obj_set_pos(label, x, y);
        return label;
    };

    lv_label_set_text(addLabel(&lv_font_montserrat_16, UiTheme::TextDim(), 0), title);

    StatColumn column;
    column.value = addLabel(&lv_font_montserrat_28, UiTheme::Text(), 18);
    column.line1 = addLabel(&lv_font_montserrat_16, UiTheme::TextDim(), 54);
    column.line2 = addLabel(&lv_font_montserrat_16, UiTheme::TextDim(), 74);
    return column;
}

// A row of buttons of which exactly one is selected.
lv_obj_t* BleTestScreen::AddSegmented(lv_obj_t* parent, const char* const* map,
                                      uint32_t selected, lv_event_cb_t onChange)
{
    lv_obj_t* m = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(m, map);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(m, true);
    lv_buttonmatrix_set_button_ctrl(m, selected, LV_BUTTONMATRIX_CTRL_CHECKED);

    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_column(m, 6, 0);

    lv_obj_set_style_bg_color(m, UiTheme::Surface(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, UiTheme::Accent(), UiTheme::Sel(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_text_color(m, UiTheme::Text(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, UiTheme::OnAccent(), UiTheme::Sel(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_text_font(m, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_radius(m, 8, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(m, 0, LV_PART_ITEMS);

    lv_obj_add_event_cb(m, onChange, LV_EVENT_VALUE_CHANGED, this);
    return m;
}

// ──────────────────────────────────────────────────────────────
// Refresh
// ──────────────────────────────────────────────────────────────

void BleTestScreen::OnShow()
{
    serviceProvider_.getBleSiteTest().SetTestMode(true);
    ApplyView();
    RefreshSummary();
}

void BleTestScreen::Refresh()
{
    RefreshSummary();
    RefreshGraph();
}

void BleTestScreen::RefreshSummary()
{
    const BleSiteTest::Summary s = serviceProvider_.getBleSiteTest().GetSummary();
    const bool linked = s.link == BleManager::LinkState::Ready;
    char buf[32];

    // Connection state
    switch (s.link)
    {
        case BleManager::LinkState::Ready:
            SetColoredText(stateLabel_, LV_SYMBOL_BULLET " Linked", UiTheme::Good());
            break;
        case BleManager::LinkState::Down:
            SetColoredText(stateLabel_, LV_SYMBOL_BULLET " No link", UiTheme::Danger());
            break;
        default:
            SetColoredText(stateLabel_, LV_SYMBOL_BULLET " Linking", UiTheme::Warn());
            break;
    }

    // RSSI
    if (linked && s.hasRssi) snprintf(buf, sizeof(buf), "%d", s.rssi);
    else                     snprintf(buf, sizeof(buf), "--");
    SetLabelText(rssi_.value, buf);
    SetLabelText(rssi_.line1, "dBm");
    if (s.hasRssi) snprintf(buf, sizeof(buf), "worst %d", s.worstRssi);
    else           buf[0] = '\0';
    SetLabelText(rssi_.line2, buf);

    // Success. Rounded down, so 99.96% does not show as a perfect 100.0%.
    const uint32_t total = s.ok + s.lost;
    if (total == 0)
    {
        SetColoredText(success_.value, "--", UiTheme::Text());
    }
    else
    {
        const uint32_t permille = static_cast<uint32_t>(uint64_t{s.ok} * 1000 / total);
        if (s.lost == 0) snprintf(buf, sizeof(buf), "100%%");
        else snprintf(buf, sizeof(buf), "%" PRIu32 ".%" PRIu32 "%%", permille / 10, permille % 10);
        lv_color_t color = permille >= 990 ? UiTheme::Good()
                         : permille >= 950 ? UiTheme::Warn()
                                           : UiTheme::Danger();
        SetColoredText(success_.value, buf, color);
    }
    snprintf(buf, sizeof(buf), "%" PRIu32 " / %" PRIu32, s.ok, total);
    SetLabelText(success_.line1, buf);
    snprintf(buf, sizeof(buf), "max run %" PRIu32, s.longestLossRun);
    SetLabelText(success_.line2, buf);

    // Latency
    if (s.lastRttMs != 0) snprintf(buf, sizeof(buf), "%" PRIu32, s.lastRttMs);
    else                  snprintf(buf, sizeof(buf), "--");
    SetLabelText(latency_.value, buf);
    if (s.maxRttMs != 0)
        snprintf(buf, sizeof(buf), "min %" PRIu32 " avg %" PRIu32, s.minRttMs, s.avgRttMs);
    else
        buf[0] = '\0';
    SetLabelText(latency_.line1, buf);
    if (s.maxRttMs != 0) snprintf(buf, sizeof(buf), "max %" PRIu32, s.maxRttMs);
    else                 buf[0] = '\0';
    SetLabelText(latency_.line2, buf);

    // Test time, disconnects, outage
    const uint32_t t = s.testSeconds;
    if (t >= 3600)
        snprintf(buf, sizeof(buf), "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32, t / 3600, (t / 60) % 60, t % 60);
    else
        snprintf(buf, sizeof(buf), "%02" PRIu32 ":%02" PRIu32, t / 60, t % 60);
    SetLabelText(time_.value, buf);
    snprintf(buf, sizeof(buf), "%" PRIu32 " disconnect%s", s.disconnects, s.disconnects == 1 ? "" : "s");
    SetLabelText(time_.line1, buf);
    if (s.currentOutageMs != 0)
    {
        FormatSeconds(buf, sizeof(buf), "down ", s.currentOutageMs);
        SetColoredText(time_.line2, buf, UiTheme::Danger());
    }
    else
    {
        if (s.longestOutageMs != 0) FormatSeconds(buf, sizeof(buf), "max ", s.longestOutageMs);
        else                        buf[0] = '\0';
        SetColoredText(time_.line2, buf, UiTheme::TextDim());
    }
}

void BleTestScreen::ApplyView()
{
    char legend[96];
    switch (view_)
    {
        case View::Rssi:
            lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
            lv_chart_set_series_color(chart_, backSeries_, UiTheme::Warn());
            lv_chart_set_series_color(chart_, frontSeries_, UiTheme::Good());
            snprintf(legend, sizeof(legend), "#%06" PRIX32 " avg#  #%06" PRIX32 " worst#  dBm",
                     Rgb(UiTheme::Good()), Rgb(UiTheme::Warn()));
            break;
        case View::Latency:
            lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
            lv_chart_set_series_color(chart_, backSeries_, UiTheme::Warn());
            lv_chart_set_series_color(chart_, frontSeries_, UiTheme::Accent());
            snprintf(legend, sizeof(legend), "#%06" PRIX32 " avg#  #%06" PRIX32 " max#  ms",
                     Rgb(UiTheme::Accent()), Rgb(UiTheme::Warn()));
            break;
        case View::ReceivedLost:
            lv_chart_set_type(chart_, LV_CHART_TYPE_STACKED);
            lv_chart_set_series_color(chart_, backSeries_, UiTheme::Good());
            lv_chart_set_series_color(chart_, frontSeries_, UiTheme::Danger());
            snprintf(legend, sizeof(legend), "#%06" PRIX32 " received#  #%06" PRIX32 " lost#",
                     Rgb(UiTheme::Good()), Rgb(UiTheme::Danger()));
            break;
    }
    SetLabelText(legendLabel_, legend);
    RefreshGraph();
}

void BleTestScreen::RefreshGraph()
{
    if (buckets_ == nullptr) return;
    serviceProvider_.getBleSiteTest().GetBuckets(buckets_, kColumns, secondsPerColumn_);

    switch (view_)
    {
        case View::Rssi:
        {
            lv_chart_set_axis_range(chart_, LV_CHART_AXIS_PRIMARY_Y, kRssiBottom, kRssiTop);
            for (uint32_t i = 0; i < kColumns; i++)
            {
                const BleSiteTest::Bucket& b = buckets_[i];
                int32_t worst = b.hasRssi ? std::clamp<int32_t>(b.rssiMin, kRssiBottom, kRssiTop)
                                          : LV_CHART_POINT_NONE;
                int32_t avg = b.hasRssi ? std::clamp<int32_t>(b.rssiAvg, kRssiBottom, kRssiTop)
                                        : LV_CHART_POINT_NONE;
                lv_chart_set_series_value_by_id(chart_, backSeries_, i, worst);
                lv_chart_set_series_value_by_id(chart_, frontSeries_, i, avg);
            }
            SetYLabels(kRssiBottom, kRssiTop, false);
            break;
        }
        case View::Latency:
        {
            uint32_t peak = 0;
            for (uint32_t i = 0; i < kColumns; i++) peak = std::max(peak, buckets_[i].rttMaxMs);
            const uint32_t top = LatencyScale(peak);

            lv_chart_set_axis_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, static_cast<int32_t>(top));
            for (uint32_t i = 0; i < kColumns; i++)
            {
                const BleSiteTest::Bucket& b = buckets_[i];
                int32_t max = b.hasRtt ? static_cast<int32_t>(std::min(b.rttMaxMs, top))
                                       : LV_CHART_POINT_NONE;
                int32_t avg = b.hasRtt ? static_cast<int32_t>(std::min(b.rttAvgMs, top))
                                       : LV_CHART_POINT_NONE;
                lv_chart_set_series_value_by_id(chart_, backSeries_, i, max);
                lv_chart_set_series_value_by_id(chart_, frontSeries_, i, avg);
            }
            SetYLabels(0, static_cast<int32_t>(top), false);
            break;
        }
        case View::ReceivedLost:
        {
            // One ping a second, so a column holds about secondsPerColumn_ of them;
            // a slow gateway tick can put one extra in, so grow to fit.
            uint32_t top = secondsPerColumn_;
            for (uint32_t i = 0; i < kColumns; i++)
                top = std::max(top, buckets_[i].ok + buckets_[i].lost);

            lv_chart_set_axis_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, static_cast<int32_t>(top));
            for (uint32_t i = 0; i < kColumns; i++)
            {
                lv_chart_set_series_value_by_id(chart_, backSeries_, i,
                                                static_cast<int32_t>(buckets_[i].ok));
                lv_chart_set_series_value_by_id(chart_, frontSeries_, i,
                                                static_cast<int32_t>(buckets_[i].lost));
            }
            SetYLabels(0, static_cast<int32_t>(top), true);
            break;
        }
    }
    lv_chart_refresh(chart_);
}

// Labels for the grid lines, top to bottom. `onlyEnds` for small integer
// scales, where the in-between lines would land on fractions.
void BleTestScreen::SetYLabels(int32_t bottom, int32_t top, bool onlyEnds)
{
    char buf[12];
    for (int i = 0; i < kYLabels; i++)
    {
        bool isEnd = (i == 0 || i == kYLabels - 1);
        if (onlyEnds && !isEnd)
            buf[0] = '\0';
        else
            snprintf(buf, sizeof(buf), "%" PRId32, top - (top - bottom) * i / (kYLabels - 1));
        SetLabelText(yLabels_[i], buf);
    }
}

// ──────────────────────────────────────────────────────────────
// Callbacks
// ──────────────────────────────────────────────────────────────

void BleTestScreen::RefreshTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<BleTestScreen*>(lv_timer_get_user_data(t));
    if (!self->IsActive()) return;
    self->Refresh();
}

void BleTestScreen::BackCb(lv_event_t* e)
{
    auto* self = static_cast<BleTestScreen*>(lv_event_get_user_data(e));
    self->serviceProvider_.getBleSiteTest().SetTestMode(false);
    self->navigator_.Go(ScreenId::Settings);
}

void BleTestScreen::ResetCb(lv_event_t* e)
{
    auto* self = static_cast<BleTestScreen*>(lv_event_get_user_data(e));
    self->serviceProvider_.getBleSiteTest().Reset();
    self->Refresh();
}

void BleTestScreen::ViewCb(lv_event_t* e)
{
    auto* self = static_cast<BleTestScreen*>(lv_event_get_user_data(e));
    uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (id > static_cast<uint32_t>(View::ReceivedLost)) return;
    self->view_ = static_cast<View>(id);
    self->ApplyView();
}

void BleTestScreen::RangeCb(lv_event_t* e)
{
    auto* self = static_cast<BleTestScreen*>(lv_event_get_user_data(e));
    uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (id >= std::size(kRangeSecondsPerColumn)) return;
    self->secondsPerColumn_ = kRangeSecondsPerColumn[id];
    self->ShowRangeLabel();
    self->RefreshGraph();
}

void BleTestScreen::ShowRangeLabel()
{
    char buf[16];
    snprintf(buf, sizeof(buf), "-%" PRIu32 "m", secondsPerColumn_ * kColumns / 60);
    SetLabelText(rangeLabel_, buf);
}
