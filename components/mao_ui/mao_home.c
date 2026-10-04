/*
 * HOME: the screen is the object. At rest it contains only the character;
 * the "MAO" wordmark is shown for a moment at boot and then gives way.
 */
#include "mao_ui_priv.h"
#include "mao_character.h"

#define WORDMARK_HOLD_MS   650
#define WORDMARK_FADE_MS   200

#define RESUME_WAKE_MS     220

static lv_obj_t *s_wordmark;
static lv_obj_t *s_fault;

void mao_home_create(lv_obj_t *scr, bool wordmark_visible)
{
    s_wordmark = lv_label_create(scr);
    lv_label_set_text(s_wordmark, "MAO");
    lv_obj_set_style_text_font(s_wordmark, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_wordmark, lv_color_hex(MAO_UI_FG), 0);
    lv_obj_set_style_text_letter_space(s_wordmark, 6, 0);
    lv_obj_align(s_wordmark, LV_ALIGN_CENTER, 3, 0);   /* +3 compensates trailing letter space */
    if (!wordmark_visible) {
        lv_obj_add_flag(s_wordmark, LV_OBJ_FLAG_HIDDEN);
    }
}

static void wordmark_opa_exec(void *obj, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void wordmark_done(lv_anim_t *a)
{
    lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN);
}

static void wake_character(lv_timer_t *t)
{
    (void)t;
    mao_character_react(MAO_CHAR_REACT_WAKE);
}

void mao_home_boot(void)
{
    /* Wordmark holds briefly, then fades while the eyes open in its place. */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_wordmark);
    lv_anim_set_exec_cb(&a, wordmark_opa_exec);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, WORDMARK_FADE_MS);
    lv_anim_set_delay(&a, WORDMARK_HOLD_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, wordmark_done);
    lv_anim_start(&a);

    lv_timer_t *t = lv_timer_create(wake_character, WORDMARK_HOLD_MS + WORDMARK_FADE_MS / 2, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* Back from deep sleep: no wordmark (MAO did not "boot", it woke up). */
void mao_home_skip_wordmark(void)
{
    lv_anim_delete(s_wordmark, wordmark_opa_exec);
    lv_obj_add_flag(s_wordmark, LV_OBJ_FLAG_HIDDEN);
}

void mao_home_resume(void)
{
    mao_home_skip_wordmark();
    lv_timer_t *t = lv_timer_create(wake_character, RESUME_WAKE_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* Fatal hardware fault: the wordmark stays, with a quiet service code
 * under it instead of the character. No debug text in normal operation. */
void mao_home_fault(const char *code)
{
    lv_anim_delete(s_wordmark, wordmark_opa_exec);
    lv_obj_set_style_text_opa(s_wordmark, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_wordmark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_wordmark, LV_ALIGN_CENTER, 3, -14);
    if (!s_fault) {
        s_fault = lv_label_create(lv_obj_get_parent(s_wordmark));
        lv_obj_set_style_text_font(s_fault, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_fault, lv_color_hex(MAO_UI_DIM), 0);
        lv_obj_set_style_text_letter_space(s_fault, 3, 0);
    }
    lv_label_set_text(s_fault, code ? code : "SERVICE");
    lv_obj_align(s_fault, LV_ALIGN_CENTER, 2, 26);
}
