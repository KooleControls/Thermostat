#include "Screen.h"

lv_obj_t* Screen::AddIconButton(lv_obj_t* parent, const char* icon)
{
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, UiTheme::IconBtn, UiTheme::IconBtn);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, UiTheme::Surface(), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);

    lv_obj_t* label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, UiTheme::TextDim(), 0);
    lv_label_set_text(label, icon);
    lv_obj_center(label);
    return btn;
}

lv_obj_t* Screen::AddHeader(lv_obj_t* root, const char* title, const char* icon)
{
    lv_obj_t* bar = lv_obj_create(root);
    lv_obj_set_size(bar, LV_PCT(100), UiTheme::HeaderH);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* btn = AddIconButton(bar, icon);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, UiTheme::Pad / 2, 0);

    lv_obj_t* label = lv_label_create(bar);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, UiTheme::Text(), 0);
    lv_label_set_text(label, title);
    lv_obj_center(label);

    return btn;
}
