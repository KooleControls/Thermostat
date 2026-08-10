#include "HomeFace.h"
#include "UiTheme.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" const lv_font_t font_temp_96;
extern "C" const lv_font_t font_icons_32;

// FontAwesome glyphs carried by font_icons_32 (see the fonts/ folder for the
// lv_font_conv line that generated it).
#define ICON_FIRE      "\xEF\x81\xAD"   // U+F06D
#define ICON_SNOWFLAKE "\xEF\x8B\x9C"   // U+F2DC
#define ICON_FAN       "\xEF\xA1\xA3"   // U+F863
#define ICON_POWER     "\xEF\x80\x91"   // U+F011

// ─────────────────────────────────────────────────────────────
// Build
// ─────────────────────────────────────────────────────────────

void HomeFace::Build(lv_obj_t* root, IntentHandler handler, void* user)
{
    handler_ = handler;
    user_    = user;

    BuildRing(root);      // behind everything — the readout sits inside it
    BuildBadge(root);
    BuildReadout(root);
    BuildNudge(root);
    BuildTiles(root);
}

// The blue→red gradient ring.
//
// LVGL's arc stroke takes a single colour, so the gradient is built from a ring
// of short arc segments with the colour interpolated per segment. They are
// static: built once, never touched by Apply(), and non-clickable so they cost
// nothing but pixels.
void HomeFace::BuildRing(lv_obj_t* root)
{
    constexpr float kStep = 360.0f / kSegments;

    for (int i = 0; i < kSegments; ++i)
    {
        float a0  = i * kStep;
        float mid = (a0 + kStep * 0.5f) * 3.14159265f / 180.0f;

        // 0 deg is 3 o'clock and angles run clockwise with y down, so cos()
        // is the horizontal position: red on the right, blue on the left,
        // blended through purple at top and bottom.
        float warm = (1.0f + cosf(mid)) * 0.5f;
        lv_color_t colour = lv_color_mix(UiTheme::Heat(), UiTheme::Cool(),
                                         (uint8_t)(warm * 255.0f));

        // The bottom of the ring sinks back into the background, which is what
        // keeps the readout the brightest thing on the screen.
        float below = sinf(mid);
        lv_opa_t opa = (lv_opa_t)(255.0f - (below > 0.0f ? below * 130.0f : 0.0f));

        lv_obj_t* seg = lv_arc_create(root);
        lv_obj_set_size(seg, kRingSize, kRingSize);
        lv_obj_align(seg, LV_ALIGN_TOP_LEFT,
                     kRingCx - kRingSize / 2, kRingCy - kRingSize / 2);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);

        // Only the background arc is drawn; indicator and knob are the parts
        // that would make this look like a slider.
        lv_obj_set_style_arc_opa(seg, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_opa(seg, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, LV_PART_MAIN);

        // Half a degree of overlap — without it the seams show as dark hairlines.
        lv_arc_set_bg_angles(seg, (int32_t)a0, (int32_t)(a0 + kStep + 0.5f));
        lv_obj_set_style_arc_width(seg, kRingWidth, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(seg, false, LV_PART_MAIN);
        lv_obj_set_style_arc_color(seg, colour, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(seg, opa, LV_PART_MAIN);
    }
}

// Top-left activity badge + the top-right bluetooth/gear pair.
void HomeFace::BuildBadge(lv_obj_t* root)
{
    badgeIcon_ = lv_label_create(root);
    lv_obj_set_style_text_font(badgeIcon_, &font_icons_32, 0);
    lv_label_set_text(badgeIcon_, ICON_FIRE);
    lv_obj_align(badgeIcon_, LV_ALIGN_TOP_LEFT, 26, 20);

    badgeArrow_ = lv_label_create(root);
    lv_obj_set_style_text_font(badgeArrow_, &lv_font_montserrat_20, 0);
    lv_label_set_text(badgeArrow_, LV_SYMBOL_UP);
    lv_obj_align(badgeArrow_, LV_ALIGN_TOP_LEFT, 64, 26);

    badgeText_ = lv_label_create(root);
    lv_obj_set_style_text_font(badgeText_, &lv_font_montserrat_20, 0);
    lv_label_set_text(badgeText_, "");
    lv_obj_align(badgeText_, LV_ALIGN_TOP_LEFT, 24, 60);

    bleIcon_ = lv_label_create(root);
    lv_obj_set_style_text_font(bleIcon_, &lv_font_montserrat_28, 0);
    lv_label_set_text(bleIcon_, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(bleIcon_, LV_ALIGN_TOP_RIGHT, -78, 24);

    lv_obj_t* gear = lv_label_create(root);
    lv_obj_set_style_text_font(gear, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(gear, UiTheme::TextDim(), 0);
    lv_label_set_text(gear, LV_SYMBOL_SETTINGS);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -22, 24);

    // The glyph is small; the hit area is not. A transparent pad over the
    // corner is what a finger actually aims at.
    lv_obj_t* gearHit = lv_button_create(root);
    lv_obj_set_size(gearHit, UiTheme::IconBtn, UiTheme::IconBtn);
    lv_obj_align(gearHit, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(gearHit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(gearHit, 0, 0);
    lv_obj_set_style_border_width(gearHit, 0, 0);
    lv_obj_add_event_cb(gearHit, IntentCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(gearHit, (void*)(uintptr_t)HomeIntent::OpenSettings);
}

// The big setpoint, its unit, and the measured-temperature line under it.
void HomeFace::BuildReadout(lv_obj_t* root)
{
    // Number and unit live in a content-sized flex row so the pair stays
    // centred in the ring when the text goes from "22" to "22.5".
    lv_obj_t* row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_CENTER, kRingCx - 240, kRingCy - 240 - 14);

    bigLabel_ = lv_label_create(row);
    lv_obj_set_style_text_color(bigLabel_, UiTheme::Text(), 0);
    lv_obj_set_style_text_font(bigLabel_, &font_temp_96, 0);
    lv_label_set_text(bigLabel_, "--");

    lv_obj_t* unit = lv_label_create(row);
    lv_obj_set_style_text_color(unit, UiTheme::Text(), 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_48, 0);
    // Both labels are top-aligned by the flex row, but the 96 px font carries a
    // far taller ascent than the 48 px one, so equal tops are not equal glyph
    // tops. Measured against the 96 px digits: 2 px lands "°C" level with them.
    lv_obj_set_style_pad_top(unit, 2, 0);
    lv_obj_set_style_pad_left(unit, 6, 0);
    lv_label_set_text(unit, "\xC2\xB0" "C");    // UTF-8 degree

    roomLabel_ = lv_label_create(root);
    lv_obj_set_style_text_color(roomLabel_, UiTheme::TextDim(), 0);
    // Kept small enough to stay inside the ring at "Current: -22.5°C" — at 28 px
    // it runs out through the stroke on both sides.
    lv_obj_set_style_text_font(roomLabel_, &lv_font_montserrat_20, 0);
    lv_label_set_text(roomLabel_, "");
    lv_obj_align(roomLabel_, LV_ALIGN_CENTER, kRingCx - 240, kRingCy - 240 + 72);
}

void HomeFace::BuildNudge(lv_obj_t* root)
{
    MakeNudge(root, false, UiTheme::Cool(), LV_ALIGN_LEFT_MID, HomeIntent::NudgeDown);
    MakeNudge(root, true,  UiTheme::Heat(), LV_ALIGN_RIGHT_MID, HomeIntent::NudgeUp);
}

// The − and + are drawn, not typeset. A font glyph at this size comes out thin
// and small inside a 76 px circle; two rounded bars give the weight the design
// asks for and put the stroke length under our control.
lv_obj_t* HomeFace::MakeNudge(lv_obj_t* root, bool plus, lv_color_t hue,
                              lv_align_t align, HomeIntent intent)
{
    constexpr int32_t kBarLen   = 34;
    constexpr int32_t kBarThick = 5;

    lv_obj_t* btn = lv_button_create(root);
    lv_obj_set_size(btn, kNudgeSize, kNudgeSize);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_color(btn, hue, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_align(btn, align, align == LV_ALIGN_LEFT_MID ? 14 : -14,
                 kRingCy - 240);
    lv_obj_add_event_cb(btn, IntentCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btn, (void*)(uintptr_t)intent);

    for (int i = 0; i < (plus ? 2 : 1); ++i)
    {
        bool vertical = (i == 1);
        lv_obj_t* bar = lv_obj_create(btn);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, vertical ? kBarThick : kBarLen,
                             vertical ? kBarLen   : kBarThick);
        lv_obj_center(bar);
        lv_obj_set_style_radius(bar, kBarThick / 2, 0);
        lv_obj_set_style_bg_color(bar, hue, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
    return btn;
}

void HomeFace::BuildTiles(lv_obj_t* root)
{
    MakeTile(root, tiles_[(int)HomeMode::Auto], 0, ICON_FAN,       "Auto",
             UiTheme::TextDim(), HomeIntent::SetAuto);
    MakeTile(root, tiles_[(int)HomeMode::Heat], 1, ICON_FIRE,      "Heating",
             UiTheme::Heat(), HomeIntent::SetHeat);
    MakeTile(root, tiles_[(int)HomeMode::Cool], 2, ICON_SNOWFLAKE, "Cooling",
             UiTheme::Cool(), HomeIntent::SetCool);
    MakeTile(root, tiles_[(int)HomeMode::Off],  3, ICON_POWER,     "Power",
             UiTheme::TextDim(), HomeIntent::SetOff);

    // Auto has no control logic behind it yet, so it is drawn but inert —
    // dimmed rather than hidden, so the row keeps the layout it will ship with.
    lv_obj_set_style_opa(tiles_[(int)HomeMode::Auto].box, LV_OPA_50, 0);
    lv_obj_remove_flag(tiles_[(int)HomeMode::Auto].box, LV_OBJ_FLAG_CLICKABLE);
}

void HomeFace::MakeTile(lv_obj_t* root, Tile& tile, int index, const char* glyph,
                        const char* text, lv_color_t hue, HomeIntent intent)
{
    constexpr int32_t kRowW = 4 * kTileW + 3 * kTileGap;
    int32_t x = (480 - kRowW) / 2 + index * (kTileW + kTileGap);

    tile.hue = hue;

    tile.box = lv_button_create(root);
    lv_obj_set_size(tile.box, kTileW, kTileH);
    lv_obj_align(tile.box, LV_ALIGN_TOP_LEFT, x, 480 - kTileH - 18);
    lv_obj_set_style_radius(tile.box, 14, 0);
    lv_obj_set_style_bg_color(tile.box, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_color(tile.box, hue, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tile.box, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(tile.box, 0, 0);
    lv_obj_set_style_border_width(tile.box, 0, 0);
    lv_obj_set_style_pad_all(tile.box, 0, 0);
    lv_obj_add_event_cb(tile.box, IntentCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(tile.box, (void*)(uintptr_t)intent);

    tile.icon = lv_label_create(tile.box);
    lv_obj_set_style_text_font(tile.icon, &font_icons_32, 0);
    lv_obj_set_style_text_color(tile.icon, hue, 0);
    lv_label_set_text(tile.icon, glyph);
    lv_obj_align(tile.icon, LV_ALIGN_TOP_MID, 0, 14);

    tile.label = lv_label_create(tile.box);
    lv_obj_set_style_text_font(tile.label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tile.label, UiTheme::TextDim(), 0);
    lv_label_set_text(tile.label, text);
    lv_obj_align(tile.label, LV_ALIGN_BOTTOM_MID, 0, -14);

    // The selection marker, parked hidden until Apply() picks a mode.
    tile.rule = lv_obj_create(tile.box);
    lv_obj_remove_style_all(tile.rule);
    lv_obj_set_size(tile.rule, kTileW - 40, 4);
    lv_obj_align(tile.rule, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_radius(tile.rule, 2, 0);
    lv_obj_set_style_bg_color(tile.rule, hue, 0);
    lv_obj_set_style_bg_opa(tile.rule, LV_OPA_COVER, 0);
    lv_obj_add_flag(tile.rule, LV_OBJ_FLAG_HIDDEN);
}

// ─────────────────────────────────────────────────────────────
// Apply
// ─────────────────────────────────────────────────────────────

// The setpoint moves in halves, so a trailing ".0" is noise on the big number —
// but ".5" is not. The measured temperature keeps its decimal either way: it is
// a reading, and a reading that silently drops a digit reads as less precise
// than it is.
void HomeFace::FormatTemp(char* out, size_t cap, float value, bool valid)
{
    if (!valid) { snprintf(out, cap, "--.-"); return; }

    if (fabsf(value - roundf(value)) < 0.05f) snprintf(out, cap, "%d", (int)lroundf(value));
    else                                      snprintf(out, cap, "%.1f", value);
}

void HomeFace::Apply(const HomeView& v)
{
    // ── The number and the measured line ─────────────────────
    char buf[24];
    FormatTemp(buf, sizeof(buf), v.setpoint, true);
    if (strcmp(lv_label_get_text(bigLabel_), buf) != 0)
        lv_label_set_text(bigLabel_, buf);

    char line[40];
    if (v.roomValid) snprintf(line, sizeof(line), "Current: %.1f\xC2\xB0" "C", v.roomTemp);
    else             snprintf(line, sizeof(line), "Current: --.-\xC2\xB0" "C");
    if (strcmp(lv_label_get_text(roomLabel_), line) != 0)
        lv_label_set_text(roomLabel_, line);

    // ── Activity badge ───────────────────────────────────────
    const char* icon = ICON_FIRE;
    const char* text = "Idle";
    lv_color_t  hue  = UiTheme::TextDim();

    switch (v.activity)
    {
    case HomeActivity::Heating:
        icon = ICON_FIRE;      text = "Heating"; hue = UiTheme::Heat(); break;
    case HomeActivity::Cooling:
        icon = ICON_SNOWFLAKE; text = "Cooling"; hue = UiTheme::Cool(); break;
    case HomeActivity::Idle:
        // Idle still shows which way the system would move, in grey.
        icon = (v.mode == HomeMode::Cool) ? ICON_SNOWFLAKE : ICON_FIRE;
        break;
    }

    if (strcmp(lv_label_get_text(badgeIcon_), icon) != 0)
        lv_label_set_text(badgeIcon_, icon);
    if (strcmp(lv_label_get_text(badgeText_), text) != 0)
        lv_label_set_text(badgeText_, text);
    lv_obj_set_style_text_color(badgeIcon_, hue, 0);
    lv_obj_set_style_text_color(badgeText_, hue, 0);

    // Called for, not running yet — the little arrow.
    const char* arrow = (v.activity == HomeActivity::Cooling ||
                         (v.activity == HomeActivity::Idle && v.mode == HomeMode::Cool))
                            ? LV_SYMBOL_DOWN : LV_SYMBOL_UP;
    if (strcmp(lv_label_get_text(badgeArrow_), arrow) != 0)
        lv_label_set_text(badgeArrow_, arrow);
    lv_obj_set_style_text_color(badgeArrow_, hue, 0);
    if (v.ramping) lv_obj_remove_flag(badgeArrow_, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_add_flag(badgeArrow_, LV_OBJ_FLAG_HIDDEN);

    // ── Gateway link ─────────────────────────────────────────
    lv_obj_set_style_text_color(bleIcon_, v.linked ? UiTheme::Accent()
                                                   : UiTheme::Surface(), 0);

    // ── Mode tiles ───────────────────────────────────────────
    for (int i = 0; i < 4; ++i)
    {
        bool selected = (i == (int)v.mode);
        lv_obj_set_style_text_color(tiles_[i].label,
                                    selected ? UiTheme::Text() : UiTheme::TextDim(), 0);
        if (selected) lv_obj_remove_flag(tiles_[i].rule, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(tiles_[i].rule, LV_OBJ_FLAG_HIDDEN);
    }
}

void HomeFace::IntentCb(lv_event_t* e)
{
    auto* self = static_cast<HomeFace*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto intent = (HomeIntent)(uintptr_t)lv_obj_get_user_data(target);
    if (self->handler_) self->handler_(self->user_, intent);
}
