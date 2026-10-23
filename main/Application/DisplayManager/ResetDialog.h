#pragma once

#include "ServiceProvider.h"
#include "lvgl.h"

// "Reset settings?" — raised by the BOOT button on the back, over whatever
// screen is up. Answering Reset puts every setting back to its default, unpairs
// the gateway and restarts; Cancel, or leaving it alone, changes nothing.
//
// Built on the top layer when shown and deleted when it goes, rather than kept
// like a screen: it is rare, and a tree built fresh each time has the current
// palette without taking part in Restyle.
//
// LVGL task only, lock held: the shell's tick and the dialog's own callbacks.
class ResetDialog
{
public:
    explicit ResetDialog(ServiceProvider& serviceProvider) : serviceProvider_(serviceProvider) {}

    ResetDialog(const ResetDialog&) = delete;
    ResetDialog& operator=(const ResetDialog&) = delete;

    void Show();    // no-op while it is already up
    void Close();   // no-op while it is not; never once the reset has begun
    bool IsOpen() const { return backdrop_ != nullptr; }

private:
    static lv_obj_t* AddButton(lv_obj_t* parent, const char* text, lv_color_t fill,
                               lv_color_t ink, lv_event_cb_t cb, void* user);
    static void CancelCb(lv_event_t* e);
    static void ResetCb(lv_event_t* e);
    static void ResetTimerCb(lv_timer_t* t);
    void Reset();

    ServiceProvider& serviceProvider_;
    lv_obj_t* backdrop_ = nullptr;
    lv_obj_t* title_    = nullptr;
    lv_obj_t* body_     = nullptr;
    lv_obj_t* buttons_  = nullptr;
    bool      resetting_ = false;
};
