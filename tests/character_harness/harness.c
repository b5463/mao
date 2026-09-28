/*
 * Character regression harness: compiles the WHOLE mao_character component
 * against a recording LVGL/FreeRTOS/ESP stub and a seeded RNG, drives it
 * through its public API on a simulated clock, and FNV-hashes - every frame
 * - all object state (z-order, visibility, geometry, colours, opacity), every
 * custom draw primitive emitted by the draw callbacks, and every log line.
 * Identical hash streams before/after a refactor = identical visuals and
 * identical decisions (including RNG consumption order).
 *
 *   harness.exe <scenario> <seed> [trace]
 * prints "frames=<n> hash=<16 hex>" and, with "trace", one hash per frame.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "esp_random.h"
#include "freertos/queue.h"
#include "mao_character.h"

/* ---------------- hashing ---------------- */
static uint64_t s_h = 1469598103934665603ULL;
static void hb(const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        s_h ^= b[i];
        s_h *= 1099511628211ULL;
    }
}
#define HV(v) hb(&(v), sizeof(v))

/* ---------------- clock / rng ---------------- */
static uint32_t s_now;
uint32_t lv_tick_get(void) { return s_now; }
static uint32_t s_rng = 1;
int64_t esp_timer_get_time(void) { return (int64_t)s_now * 1000; }
uint32_t esp_random(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ---------------- logs ---------------- */
static FILE *s_logf;
void harness_log(const char *tag, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hb(tag, strlen(tag));
    hb(buf, strlen(buf));
    if (s_logf) {
        fprintf(s_logf, "%6u %s: %s\n", s_now, tag, buf);
    }
}

/* ---------------- audio ---------------- */
void mao_audio_bump(uint8_t s) { uint32_t v = 0xB0B0 + s; HV(v); }
void mao_audio_depart(void) { uint32_t v = 0xDE9A; HV(v); }

/* ---------------- queue ---------------- */
struct hq { size_t len, item, head, count; uint8_t *buf; };
QueueHandle_t xQueueCreate(size_t len, size_t item)
{
    struct hq *q = calloc(1, sizeof(*q));
    q->len = len; q->item = item; q->buf = calloc(len, item);
    return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t w)
{
    (void)w;
    if (q->count == q->len) return pdFALSE;
    memcpy(q->buf + ((q->head + q->count) % q->len) * q->item, item, q->item);
    q->count++;
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t w)
{
    (void)w;
    if (!q->count) return pdFALSE;
    memcpy(item, q->buf + q->head * q->item, q->item);
    q->head = (q->head + 1) % q->len;
    q->count--;
    return pdTRUE;
}

/* ---------------- objects ---------------- */
struct lv_obj_t {
    int id;
    uint32_t flags;
    int32_t x, y, w, h;
    uint32_t bg, border_c;
    lv_opa_t bg_opa;
    int32_t radius, border_w;
    lv_event_cb_t cb;
    void *user;
    int align;           /* centre-aligned (make_part); custom parts are absolute */
};
static FILE *s_dump;     /* HARNESS_DUMP: every frame's drawn shapes, for tools/char_preview.py */
#define MAXO 64
static lv_obj_t s_objs[MAXO];
static int s_order[MAXO];
static int s_n;
static lv_obj_t s_screen;
lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    (void)parent;
    if (s_n >= MAXO) return NULL;
    lv_obj_t *o = &s_objs[s_n];
    memset(o, 0, sizeof(*o));
    o->id = s_n;
    s_order[s_n] = s_n;
    s_n++;
    return o;
}
void lv_obj_remove_style_all(lv_obj_t *o) { (void)o; }
void lv_obj_add_flag(lv_obj_t *o, uint32_t f) { o->flags |= f; }
void lv_obj_remove_flag(lv_obj_t *o, uint32_t f) { o->flags &= ~f; }
void lv_obj_set_align(lv_obj_t *o, int a) { o->align = 1; (void)a; }
void lv_obj_set_pos(lv_obj_t *o, int32_t x, int32_t y) { o->x = x; o->y = y; }
void lv_obj_set_size(lv_obj_t *o, int32_t w, int32_t h) { o->w = w; o->h = h; }
void lv_obj_set_style_radius(lv_obj_t *o, int32_t r, int s) { (void)s; o->radius = r; }
void lv_obj_set_style_bg_color(lv_obj_t *o, lv_color_t c, int s) { (void)s; o->bg = c.full; }
void lv_obj_set_style_bg_opa(lv_obj_t *o, lv_opa_t a, int s) { (void)s; o->bg_opa = a; }
void lv_obj_set_style_border_width(lv_obj_t *o, int32_t w, int s) { (void)s; o->border_w = w; }
void lv_obj_set_style_border_color(lv_obj_t *o, lv_color_t c, int s) { (void)s; o->border_c = c.full; }
void lv_obj_invalidate(lv_obj_t *o) { (void)o; }
void lv_obj_move_foreground(lv_obj_t *o)
{
    int k = 0;
    for (int i = 0; i < s_n; i++) {
        if (s_order[i] != o->id) s_order[k++] = s_order[i];
    }
    s_order[s_n - 1] = o->id;
}
void lv_obj_get_coords(const lv_obj_t *o, lv_area_t *a)
{
    /* LV_ALIGN_CENTER on a 240 x 240 screen, as on the device; custom-drawn
     * parts are positioned absolutely. */
    a->x1 = o->align ? (240 - o->w) / 2 + o->x : o->x;
    a->y1 = o->align ? (240 - o->h) / 2 + o->y : o->y;
    a->x2 = a->x1 + o->w - 1;
    a->y2 = a->y1 + o->h - 1;
}
void lv_obj_add_event_cb(lv_obj_t *o, lv_event_cb_t cb, int code, void *user) { (void)code; o->cb = cb; o->user = user; }
struct lv_event_t { lv_obj_t *o; };
lv_obj_t *lv_event_get_target(lv_event_t *e) { return e->o; }
void *lv_event_get_user_data(lv_event_t *e) { return e->o->user; }
lv_layer_t *lv_event_get_layer(lv_event_t *e) { (void)e; return NULL; }
void lv_draw_triangle_dsc_init(lv_draw_triangle_dsc_t *d) { memset(d, 0, sizeof(*d)); }
void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t *d) { memset(d, 0, sizeof(*d)); d->bg_opa = 255; }
void lv_draw_line_dsc_init(lv_draw_line_dsc_t *d) { memset(d, 0, sizeof(*d)); d->opa = 255; }
static lv_area_t s_clip;   /* the custom part being drawn: its own box clips it, as on the device */
void lv_draw_triangle(lv_layer_t *l, const lv_draw_triangle_dsc_t *d) { (void)l; uint32_t t = 0x7A1; HV(t); hb(d->p, sizeof(d->p)); HV(d->color.full);
    if (s_dump) fprintf(s_dump, "t %.1f %.1f %.1f %.1f %.1f %.1f %06x %d %d %d %d\n", d->p[0].x, d->p[0].y, d->p[1].x, d->p[1].y, d->p[2].x, d->p[2].y, d->color.full & 0xFFFFFF, s_clip.x1, s_clip.y1, s_clip.x2, s_clip.y2); }
void lv_draw_rect(lv_layer_t *l, const lv_draw_rect_dsc_t *d, const lv_area_t *a) { (void)l; uint32_t t = 0x7EC; HV(t); hb(a, sizeof(*a)); HV(d->bg_color.full);
    if (s_dump) fprintf(s_dump, "r %d %d %d %d %d %06x %d %d %d %d\n", a->x1, a->y1, a->x2, a->y2, d->radius, d->bg_color.full & 0xFFFFFF, s_clip.x1, s_clip.y1, s_clip.x2, s_clip.y2); }
void lv_draw_line(lv_layer_t *l, const lv_draw_line_dsc_t *d) { (void)l; uint32_t t = 0x11E; HV(t); HV(d->p1); HV(d->p2); HV(d->width);
    if (s_dump) fprintf(s_dump, "l %.1f %.1f %.1f %.1f %d %06x\n", d->p1.x, d->p1.y, d->p2.x, d->p2.y, d->width, d->color.full & 0xFFFFFF); }

/* ---------------- timers ---------------- */
struct lv_timer_t { lv_timer_cb_t cb; uint32_t period, next; };
static lv_timer_t s_timers[4];
static int s_nt;
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *user)
{
    (void)user;
    lv_timer_t *t = &s_timers[s_nt++];
    t->cb = cb; t->period = period; t->next = s_now + period;
    return t;
}

/* ---------------- frame ---------------- */
static uint32_t s_frames;
static int s_trace;
static void frame_hash(void)
{
    if (s_dump) fprintf(s_dump, "F %u\n", s_now);
    for (int i = 0; i < s_n; i++) {
        lv_obj_t *o = &s_objs[s_order[i]];
        HV(o->id); HV(o->flags);
        if (o->flags & LV_OBJ_FLAG_HIDDEN) continue;
        HV(o->x); HV(o->y); HV(o->w); HV(o->h); HV(o->bg); HV(o->bg_opa); HV(o->radius);
        HV(o->border_w); HV(o->border_c);
        if (o->cb) {
            lv_event_t e = { o };
            lv_obj_get_coords(o, &s_clip);
            o->cb(&e);   /* the custom draw output is part of the frame */
        } else if (s_dump) {
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            fprintf(s_dump, "R %d %d %d %d %d %06x %d %d %06x\n", a.x1, a.y1, a.x2, a.y2, o->radius, o->bg & 0xFFFFFF,
                    o->bg_opa, o->border_w, o->border_c & 0xFFFFFF);
        }
    }
    s_frames++;
    if (s_trace) printf("%6u %016llx\n", s_now, (unsigned long long)s_h);
#ifdef FRAME_HOOK
    FRAME_HOOK;          /* invariants.c: checks every frame */
#endif
}

/* Advance simulated time; timers fire on schedule, a "refresh" hashes the
 * scene every 33 ms like the display. */
static uint32_t s_next_refresh;
static void run_ms(uint32_t ms)
{
    const uint32_t end = s_now + ms;
    while (s_now < end) {
        s_now++;
        for (int i = 0; i < s_nt; i++) {
            if ((int32_t)(s_now - s_timers[i].next) >= 0) {
                s_timers[i].next += s_timers[i].period;
                s_timers[i].cb(&s_timers[i]);
            }
        }
        if ((int32_t)(s_now - s_next_refresh) >= 0) {
            s_next_refresh = s_now + 33;
            frame_hash();
        }
    }
}

/* ---------------- scenarios ---------------- */
static void dial_for(float dps, uint32_t ms)
{
    const uint32_t end = s_now + ms;
    float acc = 0;
    while (s_now < end) {
        acc += dps * 0.02f;
        const int32_t n = (int32_t)acc;
        if (n) { mao_character_dial(n); acc -= (float)n; }
        run_ms(20);
    }
}

static void scenario(const char *name)
{
    #define IS(x) (strcmp(name, x) == 0)
    mao_character_appear(0);
    run_ms(1500);
    if (IS("idle")) { run_ms(40000); }
    else if (IS("dial")) {
        dial_for(3, 2500); run_ms(1500); dial_for(-12, 2000); run_ms(1500);
        dial_for(40, 2500); run_ms(1500); dial_for(80, 2000); run_ms(2500);
        for (int k = 0; k < 8; k++) { mao_character_dial(k % 2 ? 2 : -2); run_ms(120); }   /* reversals */
        run_ms(4000);
    }
    else if (IS("press")) {
        for (int k = 0; k < 4; k++) { mao_character_press(true); run_ms(180); mao_character_press(false); run_ms(900); }
        mao_character_react(MAO_CHAR_REACT_WARM); run_ms(3000);
    }
    else if (IS("react")) {
        const mao_character_reaction_t rs[] = {
            MAO_CHAR_REACT_NOTICE, MAO_CHAR_REACT_ATTEND, MAO_CHAR_REACT_ACK, MAO_CHAR_REACT_BUSY,
            MAO_CHAR_REACT_IDLE, MAO_CHAR_REACT_DONE, MAO_CHAR_REACT_FAIL, MAO_CHAR_REACT_BACK,
            MAO_CHAR_REACT_DEVICE_ON, MAO_CHAR_REACT_DEVICE_OFF, MAO_CHAR_REACT_UNSURE,
        };
        for (size_t k = 0; k < sizeof(rs) / sizeof(rs[0]); k++) { mao_character_react(rs[k]); run_ms(3200); }
    }
    else if (IS("sleep")) { mao_character_set_sleepy(true); run_ms(20000); mao_character_dial(1); run_ms(4000); }
    else if (IS("menu")) { mao_character_leave(); run_ms(1500); mao_character_return(); run_ms(3000); }
    else if (IS("transfer")) {
        mao_character_transfer_search(1, 0); run_ms(300);
        mao_character_transfer_exit(1, 0); run_ms(MAO_CHAR_TRANSFER_EXIT_MS + 2000);
        mao_character_transfer_return(1, 0); run_ms(MAO_CHAR_TRANSFER_ENTER_MS + 2000);
        mao_character_transfer_fail(1, 0); run_ms(MAO_CHAR_TRANSFER_BASH_MS + 2000);
        mao_character_transfer_fail(0, -1); run_ms(MAO_CHAR_TRANSFER_BASH_MS + 1000);
        mao_character_transfer_search(-1, 0); run_ms(400); mao_character_transfer_abort(); run_ms(2000);
    }
    else if (IS("peek")) {   /* camera: first DONE, repeated DONE, interrupted DONE */
        mao_character_leave(); run_ms(1200);
        for (int k = 0; k < 4; k++) {
            mao_character_peek(true); mao_character_react(MAO_CHAR_REACT_DONE);
            run_ms(k == 0 ? 800 : 480); mao_character_peek(false); run_ms(700);
        }
        mao_character_peek(true); mao_character_react(MAO_CHAR_REACT_FAIL); run_ms(200); mao_character_peek(false);
        run_ms(1500); mao_character_return(); run_ms(2500);
    }
    else if (IS("mind")) {
        mao_character_debug_novelty(100); mao_character_react(MAO_CHAR_REACT_DEVICE_ON); run_ms(6000);
        mao_character_debug_interest(90); run_ms(12000);
        mao_character_react(MAO_CHAR_REACT_BUSY); run_ms(2500); mao_character_react(MAO_CHAR_REACT_BUSY); run_ms(2500);
        mao_character_react(MAO_CHAR_REACT_BUSY); run_ms(2500); mao_character_react(MAO_CHAR_REACT_IDLE); run_ms(9000);
    }
    else if (IS("states")) {
        const int n = mao_character_expression_count();
        for (int k = 0; k < n; k++) { mao_character_debug_expression(k); run_ms(1400); }
        run_ms(2000);
    }
    else if (IS("looks")) {
        for (int k = 0; k < mao_character_look_count(); k++) { mao_character_debug_look(k); dial_for(20, 1200); run_ms(1200); }
        mao_character_debug_look(0); run_ms(1000);
    }
    else if (IS("gather")) {   /* M4.1: the eyes make room for the interface, and come back */
        for (int k = 0; k < 3; k++) {
            mao_character_gather(); run_ms(MAO_CHAR_GATHER_MS + 600);
            mao_character_return(); run_ms(2500);
        }
    }
    else if (IS("names")) {    /* tool: index -> expression name (not part of the baseline) */
        for (int k = 0; k < mao_character_expression_count(); k++) {
            printf("%d %s\n", k, mao_character_expression_name(k));
        }
    }
    else if (IS("previews")) {
        for (int k = 0; k < MAO_CHAR_PREVIEW_COUNT - 1; k++) { mao_character_debug_preview((mao_character_preview_t)k); run_ms(3500); }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "harness <scenario> <seed> [trace] [logfile]\n"); return 2; }
    s_rng = (uint32_t)strtoul(argv[2], NULL, 10) | 1u;
    s_trace = argc > 3 && strcmp(argv[3], "trace") == 0;
    if (argc > 4) s_logf = fopen(argv[4], "w");
    if (getenv("HARNESS_DUMP")) s_dump = fopen(getenv("HARNESS_DUMP"), "w");
    s_now = 1000;
    s_next_refresh = s_now;
    if (mao_character_create(&s_screen) != 0) { fprintf(stderr, "create failed\n"); return 1; }
    mao_character_set_detents_per_rev(30);
    scenario(argv[1]);
    printf("%s seed=%s frames=%u hash=%016llx\n", argv[1], argv[2], s_frames, (unsigned long long)s_h);
    if (s_logf) fclose(s_logf);
    return 0;
}
