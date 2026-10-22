#pragma once

#include "lvgl.h"
#include "UiTheme.h"
#include <cstdint>

// A thin green line along the bottom of the screen that fills while a scan
// runs, so a wait of seconds reads as progress rather than as nothing
// happening.
//
// It fills at the pace of the scan's duration and reaches the end when the scan
// should end, so the duration it is given has to be a good one: exact for BLE,
// whose scans run for a fixed time, and learned from the last scan for WiFi.
// It used to stop at 95 % to wait for the real end, which is the stall that
// made it useless as a measure of time — the end is noticed by a 500 ms poll,
// so it could sit there most of a second even when the duration was exact.
//
// At the bottom rather than under the header, where the back button's round
// press veil reaches. Built before the screen's views, so it lies beneath them:
// the list view is transparent and shows it, and the keypad of a code or
// password view is opaque and covers it, with nothing to hide by hand.
//
// LVGL task only, like every screen. The objects belong to the screen's tree:
// a rebuild deletes them, and their animation with them, so Build() starts
// from a clean state each time.
class ScanBar
{
public:
    void Build(lv_obj_t* root)
    {
        constexpr int32_t kThick = 4;
        constexpr int32_t kBottomGap = 6;
        width_ = 480 - 2 * UiTheme::Pad;

        track_ = lv_obj_create(root);
        lv_obj_remove_style_all(track_);
        lv_obj_set_size(track_, width_, kThick);
        lv_obj_set_pos(track_, UiTheme::Pad, 480 - kBottomGap - kThick);
        lv_obj_set_style_radius(track_, kThick / 2, 0);
        lv_obj_set_style_bg_color(track_, UiTheme::Line(), 0);
        lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(track_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(track_, LV_OBJ_FLAG_HIDDEN);

        fill_ = lv_obj_create(track_);
        lv_obj_remove_style_all(fill_);
        lv_obj_set_size(fill_, 0, kThick);
        lv_obj_set_style_radius(fill_, kThick / 2, 0);
        lv_obj_set_style_bg_color(fill_, UiTheme::Good(), 0);
        lv_obj_set_style_bg_opa(fill_, LV_OPA_COVER, 0);

        running_ = false;
    }

    bool Running() const { return running_; }

    /// A scan of `durationMs` is running and is `elapsedMs` in — not zero
    /// when this screen came to a scan something else started.
    void Start(uint32_t durationMs, uint32_t elapsedMs = 0)
    {
        if (fill_ == nullptr) return;
        if (durationMs == 0) durationMs = 1;
        if (elapsedMs > durationMs) elapsedMs = durationMs;

        lv_anim_delete(fill_, SetWidth);
        lv_obj_set_width(fill_, static_cast<int32_t>(int64_t{width_} * elapsedMs / durationMs));
        lv_obj_remove_flag(track_, LV_OBJ_FLAG_HIDDEN);
        running_ = true;
        Animate(width_, durationMs - elapsedMs, nullptr);
    }

    /// The scan is over, early or late: run to the end, then hide.
    void Finish()
    {
        if (fill_ == nullptr || !running_) return;
        running_ = false;
        lv_anim_delete(fill_, SetWidth);
        Animate(width_, 200, HideWhenDone);
    }

private:
    // From the width as set, not as laid out: LVGL applies a size at its next
    // layout pass, so straight after Start() reset it to 0, lv_obj_get_width()
    // still said full — and every scan after the first ran from full to full.
    void Animate(int32_t to, uint32_t ms, lv_anim_completed_cb_t done)
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, fill_);
        lv_anim_set_exec_cb(&a, SetWidth);
        lv_anim_set_values(&a, lv_obj_get_style_width(fill_, LV_PART_MAIN), to);
        lv_anim_set_duration(&a, ms);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        if (done != nullptr) lv_anim_set_completed_cb(&a, done);
        lv_anim_start(&a);
    }

    static void SetWidth(void* fill, int32_t w)
    {
        lv_obj_set_width(static_cast<lv_obj_t*>(fill), w);
    }

    static void HideWhenDone(lv_anim_t* a)
    {
        lv_obj_add_flag(lv_obj_get_parent(static_cast<lv_obj_t*>(a->var)), LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* track_ = nullptr;   // the faint full length, so the fill reads as a fraction
    lv_obj_t* fill_ = nullptr;
    int32_t   width_ = 0;
    bool      running_ = false;
};
