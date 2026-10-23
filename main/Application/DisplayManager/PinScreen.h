#pragma once

#include "Screen.h"
#include "PinGate.h"

// The gate in front of the service menu: whose code it is, a numeric keypad, a
// masked readout and one verdict. Correct code → settings menu; cancel or the
// idle timeout → home.
//
// Knows nothing about the gateway link — it asks PinGate.
class PinScreen final : public Screen
{
public:
    PinScreen(PinGate& gate, Navigator& navigator)
        : gate_(gate), navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;   // always starts from an empty entry, owner re-read

private:
    void Append(char digit);
    void Backspace();
    void Submit();
    void Refresh();           // masked readout
    void ShowOwner();         // whose code it is

    static void KeypadCb(lv_event_t* e);
    static void CancelCb(lv_event_t* e);

    PinGate& gate_;
    Navigator& navigator_;

    lv_obj_t* ownerLabel_ = nullptr;
    lv_obj_t* entryLabel_ = nullptr;
    lv_obj_t* errorLabel_ = nullptr;

    char entry_[PinGate::Len + 1] = {};
    size_t length_ = 0;
};
