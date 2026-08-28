#include "HomeFace.h"
#include "UiTheme.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" const lv_font_t font_temp_96;
extern "C" const lv_font_t font_icons_32;

// FontAwesome glyphs carried by font_icons_32 (see the fonts/ folder for the
// lv_font_conv line that generated it).
#define ICON_POWER     "\xEF\x80\x91"   // U+F011  power-off
#define ICON_AUTO      "\xEF\x80\xA1"   // U+F021  arrows-rotate — auto changeover
#define ICON_ARROW     "\xEF\x81\xA1"   // U+F061  arrow-right
#define ICON_FIRE      "\xEF\x81\xAD"   // U+F06D  fire
#define ICON_SNOWFLAKE "\xEF\x8B\x9C"   // U+F2DC  snowflake

// ─────────────────────────────────────────────────────────────
// Build
// ─────────────────────────────────────────────────────────────

void HomeFace::Build(lv_obj_t* root, IntentHandler handler, void* user)
{
    handler_ = handler;
    user_    = user;

    BuildDisc(root);      // behind everything — the readout sits on it
    BuildBadge(root);
    BuildReadout(root);
    BuildNudge(root);
    BuildTiles(root);
}

// The dark disc the readout sits on.
//
// A filled circle rather than an outline: it lifts the number off the
// background without drawing a line around it. The vertical gradient is what
// stops it reading as a flat grey hole — top edge slightly lit, bottom sinking
// back into the background. Static, and not clickable.
void HomeFace::BuildDisc(lv_obj_t* root)
{
    lv_obj_t* disc = lv_obj_create(root);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, kDiscSize, kDiscSize);
    lv_obj_align(disc, LV_ALIGN_TOP_LEFT,
                 kDiscCx - kDiscSize / 2, kDiscCy - kDiscSize / 2);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(0x24262B), 0);
    lv_obj_set_style_bg_grad_color(disc, lv_color_hex(0x141519), 0);
    lv_obj_set_style_bg_grad_dir(disc, LV_GRAD_DIR_VER, 0);
}

// Top-left activity badge + the top-right bluetooth/gear pair.
void HomeFace::BuildBadge(lv_obj_t* root)
{
    // At rest this is one icon, coloured when that stage is running and grey
    // when it is not — no word beside it, the colour is the whole message.
    // During a changeover the row grows to three: what is running, an arrow,
    // and what it is moving to. A flex row so the two extra glyphs cost the
    // layout nothing when hidden.
    badgeRow_ = lv_obj_create(root);
    lv_obj_remove_style_all(badgeRow_);
    lv_obj_set_size(badgeRow_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(badgeRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badgeRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(badgeRow_, 10, 0);
    lv_obj_align(badgeRow_, LV_ALIGN_TOP_LEFT, 24, 20);

    badgeIcon_ = lv_label_create(badgeRow_);
    lv_obj_set_style_text_font(badgeIcon_, &font_icons_32, 0);
    lv_label_set_text(badgeIcon_, ICON_FIRE);

    badgeArrow_ = lv_label_create(badgeRow_);
    lv_obj_set_style_text_font(badgeArrow_, &font_icons_32, 0);
    lv_obj_set_style_text_color(badgeArrow_, UiTheme::TextDim(), 0);
    lv_label_set_text(badgeArrow_, ICON_ARROW);
    lv_obj_add_flag(badgeArrow_, LV_OBJ_FLAG_HIDDEN);

    badgeNext_ = lv_label_create(badgeRow_);
    lv_obj_set_style_text_font(badgeNext_, &font_icons_32, 0);
    lv_label_set_text(badgeNext_, ICON_SNOWFLAKE);
    lv_obj_add_flag(badgeNext_, LV_OBJ_FLAG_HIDDEN);

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

// The big number, its unit, and the caption that names what is being shown.
//
// The **digits** are what gets centred in the disc, not the digits-plus-unit
// pair: a "°C" wide enough to shift the number off centre is a "°C" that is too
// loud. It hangs off the top-right corner instead, small, and is re-hung by
// Apply() whenever the number changes width.
void HomeFace::BuildReadout(lv_obj_t* root)
{
    bigLabel_ = lv_label_create(root);
    lv_obj_set_style_text_color(bigLabel_, UiTheme::Text(), 0);
    lv_obj_set_style_text_font(bigLabel_, &font_temp_96, 0);
    lv_obj_set_style_text_align(bigLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(bigLabel_, "--");
    // Sits a touch below the disc's centre. Optically centring digits is not
    // the same as centring their box: the box reserves descender room the
    // digits never use, so a box-centred number reads high.
    lv_obj_align(bigLabel_, LV_ALIGN_CENTER, kDiscCx - 240, kDiscCy - 240 + 6);

    unitLabel_ = lv_label_create(root);
    lv_obj_set_style_text_color(unitLabel_, UiTheme::Text(), 0);
    lv_obj_set_style_text_font(unitLabel_, &lv_font_montserrat_20, 0);
    lv_label_set_text(unitLabel_, "\xC2\xB0" "C");   // UTF-8 degree

    // Only shown while the setpoint is on the dial. At rest the disc holds the
    // room temperature and nothing else, exactly as the design has it — the
    // caption exists so the one moment the number means something else is not
    // silent about it.
    captionLabel_ = lv_label_create(root);
    lv_obj_set_style_text_color(captionLabel_, UiTheme::TextDim(), 0);
    lv_obj_set_style_text_font(captionLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(captionLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(captionLabel_, "Setpoint");
    lv_obj_align(captionLabel_, LV_ALIGN_CENTER, kDiscCx - 240, kDiscCy - 240 + 74);
    lv_obj_add_flag(captionLabel_, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_align(btn, align, align == LV_ALIGN_LEFT_MID ? 22 : -22,
                 kDiscCy - 240);
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
    // Auto is neutral white rather than a climate hue: it is a mode, not a
    // direction, and it is the tile that has to look switched-on while the
    // other three are dimmed.
    MakeTile(root, tiles_[(int)HomeMode::Auto], 0, ICON_AUTO,      "Auto",
             UiTheme::Text(), HomeIntent::SetAuto);
    MakeTile(root, tiles_[(int)HomeMode::Heat], 1, ICON_FIRE,      "Heating",
             UiTheme::Heat(), HomeIntent::SetHeat);
    MakeTile(root, tiles_[(int)HomeMode::Cool], 2, ICON_SNOWFLAKE, "Cooling",
             UiTheme::Cool(), HomeIntent::SetCool);
    MakeTile(root, tiles_[(int)HomeMode::Off],  3, ICON_POWER,     "Power",
             UiTheme::TextDim(), HomeIntent::SetOff);
}

void HomeFace::SetTileEnabled(Tile& tile, bool enabled)
{
    lv_obj_set_style_opa(tile.box, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    if (enabled) lv_obj_add_flag(tile.box, LV_OBJ_FLAG_CLICKABLE);
    else         lv_obj_remove_flag(tile.box, LV_OBJ_FLAG_CLICKABLE);
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

// Whole degrees everywhere on this face. A thermostat that reads 21 and a
// thermostat that reads 21.4 are the same thermostat; the decimal is precision
// nobody is steering by, and dropping it is what lets the number sit big and
// centred. The tenth is still there in `climate status` for anyone debugging.
void HomeFace::FormatTemp(char* out, size_t cap, float value, bool valid)
{
    if (!valid) { snprintf(out, cap, "--"); return; }
    snprintf(out, cap, "%d", (int)lroundf(value));
}

// One badge glyph: which stage it names, and whether that stage is running.
// Grey is standby, colour is running, and no stage at all is the power glyph —
// a grey flame on a system that is set to cool would be a lie, so "none" gets
// its own icon rather than borrowing heating's.
void HomeFace::SetBadgeGlyph(lv_obj_t* label, HomeStage what, bool running)
{
    const char* icon = ICON_POWER;
    lv_color_t  hue  = UiTheme::TextDim();

    if (what == HomeStage::Heating)
    {
        icon = ICON_FIRE;
        if (running) hue = UiTheme::Heat();
    }
    else if (what == HomeStage::Cooling)
    {
        icon = ICON_SNOWFLAKE;
        if (running) hue = UiTheme::Cool();
    }

    if (strcmp(lv_label_get_text(label), icon) != 0) lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, hue, 0);
}

void HomeFace::Apply(const HomeView& v)
{
    // ── The number ───────────────────────────────────────────
    // The disc holds the room temperature; nudging swaps it for the setpoint
    // and the caption appears to say so, until the revert puts it back.
    char buf[24];
    if (v.showSetpoint) FormatTemp(buf, sizeof(buf), v.setpoint, true);
    else                FormatTemp(buf, sizeof(buf), v.roomTemp, v.roomValid);

    if (strcmp(lv_label_get_text(bigLabel_), buf) != 0)
    {
        lv_label_set_text(bigLabel_, buf);
        // The unit is anchored to the number, and the number just changed
        // width ("9" to "22"), so the anchor has to be recomputed. align_to is
        // a one-shot calculation, not a standing relationship.
        lv_obj_update_layout(bigLabel_);
        lv_obj_align_to(unitLabel_, bigLabel_, LV_ALIGN_OUT_RIGHT_TOP, 2, kUnitDrop);
    }

    if (v.showSetpoint) lv_obj_remove_flag(captionLabel_, LV_OBJ_FLAG_HIDDEN);
    else                lv_obj_add_flag(captionLabel_, LV_OBJ_FLAG_HIDDEN);

    // ── Activity badge ───────────────────────────────────────
    // Grey means that stage is not running; the colour means it is. A grey
    // flame is "not heating", nothing more.
    //
    // A changeover — running one stage while the other is already called for —
    // is the one case a single icon cannot say, so it becomes three glyphs:
    // what is running (in colour), an arrow, and what it is moving to (grey,
    // because it has not started).
    bool changeover = v.movingTo != v.stage &&
                      v.stage    != HomeStage::None &&
                      v.movingTo != HomeStage::None;

    SetBadgeGlyph(badgeIcon_, v.stage, v.running);

    if (changeover)
    {
        SetBadgeGlyph(badgeNext_, v.movingTo, false);
        lv_obj_remove_flag(badgeArrow_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(badgeNext_,  LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(badgeArrow_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(badgeNext_,  LV_OBJ_FLAG_HIDDEN);
    }

    // ── Gateway link ─────────────────────────────────────────
    lv_obj_set_style_text_color(bleIcon_, v.linked ? UiTheme::Accent()
                                                   : UiTheme::Surface(), 0);

    // ── Mode tiles ───────────────────────────────────────────
    // Cooling is the only tile that can be unavailable, and it is dimmed
    // rather than hidden so the row keeps its shape on every installation.
    SetTileEnabled(tiles_[(int)HomeMode::Cool], v.coolingAvailable);

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
