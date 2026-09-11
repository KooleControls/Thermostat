#pragma once

#include "UiTheme.h"
#include "lvgl.h"

// Which page the shell should show. Add an id when a new screen class lands
// (the firmware service screen is the next one).
enum class ScreenId
{
    Home,
    Pin,        // resolved to Settings by the shell when no PIN is set
    Settings,
    Wifi,
    Ble,
    Info,
};

// All a screen may ask of the shell. Screens depend on this, never on
// DisplayManager — that is what keeps the include graph acyclic while the
// shell owns every screen by value.
class Navigator
{
public:
    virtual ~Navigator() = default;
    virtual void Go(ScreenId id) = 0;

    /// Re-read the palette and rebuild every screen. Only the settings screen
    /// calls this, and only because the theme toggle lives on it; it is here
    /// rather than reached through DisplayManager so screens keep their single
    /// dependency on the shell.
    virtual void Restyle() = 0;
};

// One full-screen page.
//
// The LVGL object tree is built once, on first load, and then kept: there are
// only a handful of screens and switching has to feel instant. Dynamic content
// belongs in OnShow(), which runs on every load.
//
// Threading: Build(), OnShow(), and every LVGL timer/event callback run with
// the LVGL lock already held (esp_lvgl_port holds it across lv_timer_handler,
// and the shell takes it around Load()). Screens therefore never lock.
class Screen
{
public:
    virtual ~Screen() = default;

    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    void Load()
    {
        if (root_ == nullptr)
        {
            root_ = lv_obj_create(nullptr);
            lv_obj_set_style_bg_color(root_, UiTheme::Bg(), 0);
            lv_obj_set_style_pad_all(root_, 0, 0);
            lv_obj_set_style_border_width(root_, 0, 0);
            lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
            Build(root_);
        }
        OnShow();
        lv_screen_load(root_);
    }

    /// True while this screen is the one on the panel — timers use it to skip
    /// work for screens that are built but not visible.
    bool IsActive() const { return root_ != nullptr && lv_screen_active() == root_; }

    /// Give up the object tree without freeing it, so the next Load() builds a
    /// fresh one. The screen object itself survives, so anything holding a
    /// `Screen*` — the shell, an lv_timer's user data — stays valid.
    ///
    /// The caller takes ownership of what comes back and must delete it. This
    /// exists for the screen that is *on the panel*: LVGL keeps its own pointer
    /// to the active screen and nulls it if that screen is deleted, so the
    /// replacement has to be loaded while the original is still alive.
    lv_obj_t* Detach()
    {
        if (root_ == nullptr) return nullptr;   // never built — nothing to hand over
        OnDestroy();
        lv_obj_t* outgoing = root_;
        root_ = nullptr;
        return outgoing;
    }

    /// Detach and free in one go — for screens that are built but not showing.
    void Rebuild()
    {
        lv_obj_t* outgoing = Detach();
        if (outgoing) lv_obj_delete(outgoing);   // takes every child and its local styles
    }

protected:
    Screen() = default;

    virtual void Build(lv_obj_t* root) = 0;
    virtual void OnShow() {}

    /// Last call before the tree is deleted. Override to drop anything that
    /// outlives it and would then be left pointing into it — in practice that
    /// means lv_timers, which are not children of the root and so survive the
    /// delete with stale widget pointers in reach.
    virtual void OnDestroy() {}

    // ── Shared chrome ────────────────────────────────────────
    /// Title bar with a leading icon button (back/close). Returns the button so
    /// the caller can attach its own handler.
    static lv_obj_t* AddHeader(lv_obj_t* root, const char* title, const char* icon);

    /// Borderless, transparent icon button — the gear, close and back affordances.
    static lv_obj_t* AddIconButton(lv_obj_t* parent, const char* icon);

    /// lv_label_set_text() that skips identical text.
    ///
    /// LVGL invalidates a label on every set_text, whether or not the string
    /// changed, and an invalidated area is re-rendered and re-flushed. Screens
    /// poll their managers on a timer and mostly write back what is already
    /// there, so the unguarded call turns "nothing happened" into real work:
    /// measured at 25-35 ms of software rendering plus a PSRAM flush burst once
    /// a second on the home screen, for pixels that do not change. That burst
    /// is also what makes the panel visibly glitch, because the flush contends
    /// with the RGB bounce-buffer DMA for PSRAM bandwidth.
    ///
    /// Use this on every periodic path. Build() may call lv_label_set_text
    /// directly — it runs once.
    static void SetLabelText(lv_obj_t* label, const char* text);

    lv_obj_t* root_ = nullptr;
};
