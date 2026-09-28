/*
 * Circle-aware layout (M4.1). The display is a 240 px circle: the usable
 * width of a row depends on its height. Words are measured once when their
 * text changes and, if needed, tightened to fit the chord of their row -
 * never shrunk to another size, never clipped by the rim.
 */
#include <math.h>
#include <string.h>
#include "mao_ui_priv.h"

#define SCREEN_R      120.0f
#define RIM_MARGIN    10.0f      /* px kept clear of the rim at a row's ends */

float mao_ui_chord_half(float y)
{
    const float h = SCREEN_R * SCREEN_R - y * y;
    return h > 0.0f ? sqrtf(h) - RIM_MARGIN : 0.0f;
}

int mao_ui_text_width(const lv_font_t *font, int32_t track, const char *txt)
{
    if (!txt || !txt[0]) {
        return 0;
    }
    lv_point_t sz;
    lv_text_get_size(&sz, txt, font, track, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return (int)sz.x;
}

int mao_ui_fit(lv_obj_t *label, const lv_font_t *font, int32_t track, float y, int32_t *applied)
{
    const char *txt = lv_label_get_text(label);
    const float room = 2.0f * mao_ui_chord_half(y);
    int32_t t = track;
    int w = mao_ui_text_width(font, t, txt);
    /* Tighten the tracking first (it is decoration); stop at 0. */
    while (w > room && t > 0) {
        t--;
        w = mao_ui_text_width(font, t, txt);
    }
    if (!applied || *applied != t) {
        lv_obj_set_style_text_letter_space(label, t, 0);
        if (applied) {
            *applied = t;
        }
    }
    return w;
}
