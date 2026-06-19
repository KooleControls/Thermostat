#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Display.h"
#include "Touch.h"
#include "Knob.h"
#include "lvgl.h"

// DisplayManager owns the display subsystem: it brings up the panel and touch
// hardware (via the hardware/ drivers), starts the LVGL port, and is the home
// for the UI — what we actually show on screen. Build UI under
// lvgl_port_lock()/unlock(); the LVGL stack runs in its own task after Init().
class DisplayManager
{
    static constexpr const char *TAG = "DisplayManager";

public:
    explicit DisplayManager(ServiceProvider &serviceProvider);

    DisplayManager(const DisplayManager &) = delete;
    DisplayManager &operator=(const DisplayManager &) = delete;
    DisplayManager(DisplayManager &&) = delete;
    DisplayManager &operator=(DisplayManager &&) = delete;

    void Init();

    lv_display_t *getDisplay() { return lvDisplay_; }

private:
    bool InitLvgl();
    void InitTouch();
    void InitKnob();
    void BuildUi();

    ServiceProvider &serviceProvider_;
    InitState initState_;

    Display display_;
    Touch touch_;
#ifdef BOARD_HAS_KNOB
    Knob knob_;
#endif
    lv_display_t *lvDisplay_ = nullptr;
};
