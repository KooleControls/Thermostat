#include "PinScreen.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "PinScreen";

// 3×4 keypad. The trailing "" terminates the map (LVGL convention).
static const char* kKeypadMap[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""
};

// Top to bottom under the header: whose code it is (up to three lines), the
// masked entry, the verdict, and the keypad in what is left. The keypad gave
// up some height to the owner lines; its keys are still over 50 px tall.
static constexpr int32_t kOwnerY = UiTheme::HeaderH;
static constexpr int32_t kEntryY = kOwnerY + 72;
static constexpr int32_t kErrorY = kEntryY + 56;
static constexpr int32_t kPadY   = kErrorY + 28;

void PinScreen::Build(lv_obj_t* root)
{
    lv_obj_t* cancel = AddHeader(root, "Enter code", LV_SYMBOL_CLOSE);
    lv_obj_add_event_cb(cancel, CancelCb, LV_EVENT_CLICKED, this);

    ownerLabel_ = lv_label_create(root);
    lv_obj_set_width(ownerLabel_, 480 - 2 * UiTheme::Pad);
    lv_obj_set_style_text_font(ownerLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ownerLabel_, UiTheme::TextDim(), 0);
    lv_obj_set_style_text_align(ownerLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ownerLabel_, LV_ALIGN_TOP_MID, 0, kOwnerY);

    entryLabel_ = lv_label_create(root);
    lv_obj_set_style_text_font(entryLabel_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(entryLabel_, UiTheme::Text(), 0);
    lv_obj_set_style_text_letter_space(entryLabel_, 8, 0);
    lv_obj_align(entryLabel_, LV_ALIGN_TOP_MID, 0, kEntryY);

    errorLabel_ = lv_label_create(root);
    lv_obj_set_style_text_font(errorLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(errorLabel_, UiTheme::Danger(), 0);
    lv_label_set_text(errorLabel_, "");
    lv_obj_align(errorLabel_, LV_ALIGN_TOP_MID, 0, kErrorY);

    lv_obj_t* pad = lv_buttonmatrix_create(root);
    lv_buttonmatrix_set_map(pad, kKeypadMap);
    lv_obj_set_size(pad, 480 - 2 * UiTheme::Pad, 480 - UiTheme::Pad - kPadY);
    lv_obj_align(pad, LV_ALIGN_BOTTOM_MID, 0, -UiTheme::Pad);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);
    lv_obj_set_style_text_font(pad, &lv_font_montserrat_28, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pad, UiTheme::Text(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pad, UiTheme::Surface(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pad, UiTheme::Accent(), UiTheme::Sel(LV_PART_ITEMS, LV_STATE_PRESSED));
    lv_obj_set_style_radius(pad, 8, LV_PART_ITEMS);
    lv_obj_add_event_cb(pad, KeypadCb, LV_EVENT_VALUE_CHANGED, this);
}

void PinScreen::OnShow()
{
    entry_[0] = '\0';
    length_ = 0;
    Refresh();
    ShowOwner();
    lv_label_set_text(errorLabel_, "");
}

void PinScreen::Refresh()
{
    // Masked readout — one bullet per entered digit, a hint when empty.
    char masked[PinGate::Len * 3 + 1] = {};   // U+2022 is 3 bytes in UTF-8
    size_t w = 0;
    for (size_t i = 0; i < length_; i++)
    {
        std::memcpy(masked + w, "\xE2\x80\xA2", 3);
        w += 3;
    }
    masked[w] = '\0';
    lv_label_set_text(entryLabel_, length_ == 0 ? "- - - - - -" : masked);
}

// Never the code itself: the gateway it belongs to, which tells an installer
// which code to type.
void PinScreen::ShowOwner()
{
    char name[40];
    char id[16];
    char text[80];
    if (gate_.Owner(name, sizeof(name), id, sizeof(id)))
        snprintf(text, sizeof(text), "%s\n%s%s%s",
                 gate_.UsesInstallCode() ? "Install code of" : "Paired with",
                 name, (name[0] != '\0' && id[0] != '\0') ? "\n" : "", id);
    else
        snprintf(text, sizeof(text), "No gateway paired");
    lv_label_set_text(ownerLabel_, text);
}

void PinScreen::Append(char digit)
{
    if (length_ >= PinGate::Len) return;
    entry_[length_++] = digit;
    entry_[length_] = '\0';
    lv_label_set_text(errorLabel_, "");
    Refresh();
}

void PinScreen::Backspace()
{
    if (length_ == 0) return;
    entry_[--length_] = '\0';
    Refresh();
}

void PinScreen::Submit()
{
    if (gate_.Check(entry_))
    {
        navigator_.Go(ScreenId::Settings);
        return;
    }
    ESP_LOGW(TAG, "Wrong code entered");
    entry_[0] = '\0';
    length_ = 0;
    Refresh();
    lv_label_set_text(errorLabel_, "Wrong code");
}

void PinScreen::KeypadCb(lv_event_t* e)
{
    auto* self = static_cast<PinScreen*>(lv_event_get_user_data(e));
    lv_obj_t* pad = lv_event_get_target_obj(e);
    const char* txt = lv_buttonmatrix_get_button_text(pad, lv_buttonmatrix_get_selected_button(pad));
    if (txt == nullptr) return;

    if (std::strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)   self->Backspace();
    else if (std::strcmp(txt, LV_SYMBOL_OK) == 0)     self->Submit();
    else if (txt[0] >= '0' && txt[0] <= '9')          self->Append(txt[0]);
}

void PinScreen::CancelCb(lv_event_t* e)
{
    static_cast<PinScreen*>(lv_event_get_user_data(e))->navigator_.Go(ScreenId::Home);
}
