#pragma once

#include "UiTheme.h"
#include "lvgl.h"

// Which page the shell should show. Add an id when a new screen class lands
// (the WiFi and firmware service screens are the next two).
enum class ScreenId
{
    Home,
    Pin,        // resolved to Settings by the shell when no PIN is set
    Settings,
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

protected:
    Screen() = default;

    virtual void Build(lv_obj_t* root) = 0;
    virtual void OnShow() {}

    // ── Shared chrome ────────────────────────────────────────
    /// Title bar with a leading icon button (back/close). Returns the button so
    /// the caller can attach its own handler.
    static lv_obj_t* AddHeader(lv_obj_t* root, const char* title, const char* icon);

    /// Borderless, transparent icon button — the gear, close and back affordances.
    static lv_obj_t* AddIconButton(lv_obj_t* parent, const char* icon);

    lv_obj_t* root_ = nullptr;
};
