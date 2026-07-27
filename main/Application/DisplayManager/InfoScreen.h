#pragma once

#include "Screen.h"
#include "ServiceProvider.h"

// Read-only "what am I" page: device name, firmware version, network state and a
// couple of health numbers. Rebuilt values on every show, so it is always the
// live picture rather than whatever was true when the screen was first opened.
class InfoScreen final : public Screen
{
public:
    InfoScreen(ServiceProvider& serviceProvider, Navigator& navigator)
        : serviceProvider_(serviceProvider), navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;

private:
    enum Row { Name, Version, Mode, Ip, Mac, Uptime, Heap, RowCount };

    void AddRow(lv_obj_t* list, Row row, const char* label);
    static void BackCb(lv_event_t* e);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    lv_obj_t* values_[RowCount] = {};
};
