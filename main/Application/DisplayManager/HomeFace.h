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

/// Which stage the system is in — the top-left badge. None means neither, i.e.
/// off. Whether that stage is *running* is a separate bit (HomeView::running),
/// because "in heating, not currently burning" is standby, not off, and the two
/// look different on the panel.
enum class HomeStage : uint8_t { None, Heating, Cooling };

struct HomeView
{
    float setpoint  = 20.0f;
    float roomTemp  = 0.0f;
    bool  roomValid = false;

    /// The big number is the room temperature, except while the setpoint is
    /// being nudged — then the two swap, and the line beneath always carries
    /// whichever value the centre is not showing.
    bool showSetpoint = false;

    HomeMode     mode     = HomeMode::Off;

    /// The stage the system is in, and whether it is firing rather than
    /// standing by. Colour says running; the glyph says which stage.
    HomeStage stage   = HomeStage::None;
    bool      running = false;

    /// What the stage is on its way to. When it differs from `stage` and
    /// neither is None the badge spells the changeover out — flame → snowflake
    /// — because a system held on its old mode while timers run should say so.
    /// Equal values mean no changeover, and the badge stays one icon.
    HomeStage movingTo = HomeStage::None;

    /// BLE link to the gateway is up — lights the bluetooth glyph.
    bool linked = false;

    /// Whether the installation can cool at all — the gateway says so over
    /// OpenTherm (slave config), exactly as it would to any thermostat. False
    /// dims the Cooling tile and makes it untouchable, so a guest is not
    /// offered a mode the building cannot deliver.
    bool coolingAvailable = true;
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
    static constexpr int32_t kDiscSize = 236;    // the dark disc behind the number
    static constexpr int32_t kDiscCx   = 240;
    static constexpr int32_t kDiscCy   = 208;

    /// How far the unit's box drops below the number's box so the two glyph
    /// tops line up. The 96 px font's ascent is much taller than the 28 px
    /// one's, so equal boxes are not equal glyphs — measured, not derived.
    static constexpr int32_t kUnitDrop = 5;

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

    void BuildDisc(lv_obj_t* root);
    void BuildBadge(lv_obj_t* root);
    void BuildReadout(lv_obj_t* root);
    void BuildNudge(lv_obj_t* root);
    void BuildTiles(lv_obj_t* root);

    lv_obj_t* MakeNudge(lv_obj_t* root, bool plus, lv_color_t hue,
                        lv_align_t align, HomeIntent intent);
    void      MakeTile(lv_obj_t* root, Tile& tile, int index, const char* glyph,
                       const char* text, lv_color_t hue, HomeIntent intent);
    static void SetTileEnabled(Tile& tile, bool enabled);

    static void IntentCb(lv_event_t* e);
    static void FormatTemp(char* out, size_t cap, float value, bool valid);
    static void SetBadgeGlyph(lv_obj_t* label, HomeStage what, bool running);

    IntentHandler handler_ = nullptr;
    void*         user_    = nullptr;

    // The badge is a flex row: one icon at rest, three during a changeover.
    lv_obj_t* badgeRow_     = nullptr;
    lv_obj_t* badgeIcon_    = nullptr;   // what is running (or would)
    lv_obj_t* badgeArrow_   = nullptr;   // shown only during a changeover
    lv_obj_t* badgeNext_    = nullptr;   // what it is moving to
    lv_obj_t* bleIcon_      = nullptr;
    lv_obj_t* bigLabel_     = nullptr;
    lv_obj_t* unitLabel_    = nullptr;
    lv_obj_t* captionLabel_ = nullptr;

    Tile tiles_[4];   // Auto, Heat, Cool, Off — indexed by HomeMode
};
