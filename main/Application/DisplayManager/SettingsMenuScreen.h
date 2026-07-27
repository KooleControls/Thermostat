#pragma once

#include "Screen.h"

// The service menu behind the gear: a scrollable column of rows, each leading to
// its own purpose-built screen.
//
// Rows for features that don't exist yet are added disabled — visible so the
// menu reads as complete, unresponsive so there is no dead navigation. Enabling
// one is a two-line change in Build() plus the screen it points at.
class SettingsMenuScreen final : public Screen
{
public:
    explicit SettingsMenuScreen(Navigator& navigator) : navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;

private:
    /// One menu row. `target` is where a tap goes; pass a null `target` (via
    /// AddPendingRow) for a feature that hasn't landed.
    lv_obj_t* AddRow(lv_obj_t* list, const char* icon, const char* text, ScreenId target);
    lv_obj_t* AddPendingRow(lv_obj_t* list, const char* icon, const char* text);

    static void RowCb(lv_event_t* e);
    static void CloseCb(lv_event_t* e);

    lv_obj_t* MakeRow(lv_obj_t* list, const char* icon, const char* text, const char* trailing);

    Navigator& navigator_;
};
