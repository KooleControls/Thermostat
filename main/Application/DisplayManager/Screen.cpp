#include "Screen.h"
#include <cstring>

void Screen::SetLabelText(lv_obj_t* label, const char* text)
{
    if (label == nullptr || text == nullptr) return;

    const char* current = lv_label_get_text(label);
    if (current != nullptr && strcmp(current, text) == 0) return;

    lv_label_set_text(label, text);
}

lv_obj_t* Screen::AddIconButton(lv_obj_t* parent, const char* icon)
{
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, UiTheme::IconBtn, UiTheme::IconBtn);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    // Press() and not Surface(): a tint of the card colour over a background
    // the card colour already resembles is no feedback at all — on the dark
    // palette #1C1C1E at 20 % over black lands on #050506.
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, UiTheme::Press(), LV_STATE_PRESSED);
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

// ── The card, and the menu row built on it ──────────────────────────────
//
// Geometry is derived from the panel width rather than written down. The
// numbers that were fixed here first (a 300 px label, a 200 px tail) are
// exactly the ones that break when a panel changes size: a name clipped at
// 300 px on a 448 px card reads as a bug, not as a design.
namespace
{
    /// A card fills the list, and the list is inset by the gutter on both sides.
    int32_t CardWidth()
    {
        return lv_display_get_horizontal_resolution(nullptr) - 2 * UiTheme::Pad;
    }

    // Where a menu row's text column starts, and what it has to clear on the
    // right. The icon box plus a gutter either side of it; then the chevron,
    // or the switch on the one row that carries one.
    constexpr int32_t kTextX = UiTheme::Pad / 2 + UiTheme::IconBox + 14;
}

lv_obj_t* Screen::AddCard(lv_obj_t* parent, int32_t height)
{
    lv_obj_t* card = lv_button_create(parent);
    lv_obj_set_size(card, LV_PCT(100), height);
    lv_obj_set_style_bg_color(card, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_color(card, UiTheme::SurfacePressed(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(card, UiTheme::Radius, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UiTheme::Line(), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_hor(card, UiTheme::Pad, 0);
    return card;
}

Screen::MenuRow Screen::AddMenuRow(lv_obj_t* list, const char* icon, const char* title,
                                   const char* subtitle, bool chevron,
                                   int32_t reserveRight)
{
    lv_obj_t* card = AddCard(list, UiTheme::RowH);

    // The tinted square. Its own object rather than a background on the label,
    // because the glyph has to be centred in a fixed box whatever its width —
    // a padlock and a chevron are not the same number of pixels wide.
    lv_obj_t* box = lv_obj_create(card);
    lv_obj_set_size(box, UiTheme::IconBox, UiTheme::IconBox);
    lv_obj_align(box, LV_ALIGN_LEFT_MID, -UiTheme::Pad / 2, 0);
    lv_obj_set_style_bg_color(box, UiTheme::AccentSoft(), 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    // The box is inside the button and must not eat the tap: the whole row is
    // the touch target, and a press that lands on the icon has to feel the same
    // as one that lands beside it.
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* glyph = lv_label_create(box);
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(glyph, UiTheme::Accent(), 0);
    lv_label_set_text(glyph, icon);
    lv_obj_center(glyph);

    const int32_t textW = CardWidth() - kTextX - reserveRight;

    lv_obj_t* name = lv_label_create(card);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(name, UiTheme::Text(), 0);
    lv_obj_set_width(name, textW);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(name, title);

    lv_obj_t* sub = nullptr;
    if (subtitle != nullptr)
    {
        // Two lines: the title rides above centre and the subtitle below it.
        lv_obj_align(name, LV_ALIGN_LEFT_MID, kTextX - UiTheme::Pad, -13);

        sub = lv_label_create(card);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(sub, UiTheme::TextDim(), 0);
        lv_obj_set_width(sub, textW);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(sub, subtitle);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, kTextX - UiTheme::Pad, 13);
    }
    else
    {
        lv_obj_align(name, LV_ALIGN_LEFT_MID, kTextX - UiTheme::Pad, 0);
    }

    if (chevron)
    {
        lv_obj_t* mark = lv_label_create(card);
        lv_obj_set_style_text_font(mark, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(mark, UiTheme::TextDim(), 0);
        lv_label_set_text(mark, LV_SYMBOL_RIGHT);
        lv_obj_align(mark, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    return MenuRow{ card, sub };
}
