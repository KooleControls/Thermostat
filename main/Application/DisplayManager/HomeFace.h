#pragma once

#include "lvgl.h"
#include <cstdint>

// The thermostat face, as pure presentation.
//
// Nothing here knows about a manager, a ServiceProvider, or OpenTherm: it takes
// a HomeView of plain values, draws it, and raises a HomeIntent when a finger
// lands. That seam is deliberate — it is what lets this exact file compile in
// the browser LVGL simulator against fake data, so restyling can be iterated in
// a browser instead of a flash cycle. Keep it dependency-free.
//
// HomeScreen owns the other half: pulling the values and acting on the intents.

/// Which of the four bottom tiles is lit.
enum class HomeMode : uint8_t { Auto, Heat, Cool, Off };

/// What the system is doing right now — the top-left badge.
enum class HomeActivity : uint8_t { Idle, Heating, Cooling };

struct HomeView
{
    float setpoint  = 20.0f;   // the big number
    float roomTemp  = 0.0f;    // the "Current:" line
    bool  roomValid = false;

    HomeMode     mode     = HomeMode::Off;
    HomeActivity activity = HomeActivity::Idle;

    /// Demand is on but the boiler has not caught up yet — draws the arrow next
    /// to the badge. Off once the flame (or the cooling stage) actually runs.
    bool ramping = false;

    /// BLE link to the gateway is up — lights the bluetooth glyph.
    bool linked = false;
};

/// What a touch means. The face raises these; it never acts on them.
enum class HomeIntent : uint8_t
{
    NudgeDown,
    NudgeUp,
    SetAuto,
    SetHeat,
    SetCool,
    SetOff,
    OpenSettings,
};

class HomeFace
{
public:
    using IntentHandler = void (*)(void* user, HomeIntent intent);

    /// Builds the whole tree into `root`. Call once.
    void Build(lv_obj_t* root, IntentHandler handler, void* user);

    /// Pushes a new state onto the built tree. Cheap to call on a timer —
    /// every write is guarded against writing back what is already there.
    void Apply(const HomeView& view);

private:
    // ── Geometry (480x480) ───────────────────────────────────────
    static constexpr int32_t kRingSize  = 270;   // outer diameter
    static constexpr int32_t kRingCx    = 240;
    static constexpr int32_t kRingCy    = 198;
    static constexpr int32_t kRingWidth = 6;
    static constexpr int32_t kSegments  = 24;    // gradient is built from arcs

    static constexpr int32_t kNudgeSize = 76;    // -/+ circle diameter
    static constexpr int32_t kTileW     = 100;
    static constexpr int32_t kTileH     = 96;
    static constexpr int32_t kTileGap   = 10;

    struct Tile
    {
        lv_obj_t* box   = nullptr;
        lv_obj_t* icon  = nullptr;
        lv_obj_t* label = nullptr;
        lv_obj_t* rule  = nullptr;   // selection underline
        lv_color_t hue{};
    };

    void BuildRing(lv_obj_t* root);
    void BuildBadge(lv_obj_t* root);
    void BuildReadout(lv_obj_t* root);
    void BuildNudge(lv_obj_t* root);
    void BuildTiles(lv_obj_t* root);

    lv_obj_t* MakeNudge(lv_obj_t* root, bool plus, lv_color_t hue,
                        lv_align_t align, HomeIntent intent);
    void      MakeTile(lv_obj_t* root, Tile& tile, int index, const char* glyph,
                       const char* text, lv_color_t hue, HomeIntent intent);

    static void IntentCb(lv_event_t* e);
    static void FormatTemp(char* out, size_t cap, float value, bool valid);

    IntentHandler handler_ = nullptr;
    void*         user_    = nullptr;

    lv_obj_t* badgeIcon_  = nullptr;
    lv_obj_t* badgeText_  = nullptr;
    lv_obj_t* badgeArrow_ = nullptr;
    lv_obj_t* bleIcon_    = nullptr;
    lv_obj_t* bigLabel_   = nullptr;
    lv_obj_t* roomLabel_  = nullptr;

    Tile tiles_[4];   // Auto, Heat, Cool, Off — indexed by HomeMode
};
