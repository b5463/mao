/* Recording LVGL stub for the character regression harness. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define LV_HOR_RES 240
#define LV_VER_RES 240
typedef float lv_value_precise_t;
typedef struct { lv_value_precise_t x, y; } lv_point_precise_t;
typedef struct { int32_t x1, y1, x2, y2; } lv_area_t;
typedef struct { uint32_t full; } lv_color_t;
typedef uint8_t lv_opa_t;
typedef struct lv_layer_t lv_layer_t;
typedef struct lv_obj_t lv_obj_t;
typedef struct lv_event_t lv_event_t;
typedef struct lv_timer_t lv_timer_t;
typedef void (*lv_event_cb_t)(lv_event_t *e);
typedef void (*lv_timer_cb_t)(lv_timer_t *t);
enum { LV_OBJ_FLAG_HIDDEN = 1, LV_OBJ_FLAG_CLICKABLE = 2, LV_OBJ_FLAG_SCROLLABLE = 4 };
enum { LV_EVENT_DRAW_MAIN = 1 };
enum { LV_ALIGN_CENTER = 9 };
#define LV_RADIUS_CIRCLE 0x7FFF
#define LV_OPA_TRANSP 0
#define LV_OPA_COVER 255
#define LV_OPA_60 153
#define LV_OPA_90 229
typedef struct { lv_point_precise_t p[3]; lv_color_t color; lv_opa_t opa; } lv_draw_triangle_dsc_t;
typedef struct { lv_color_t bg_color; lv_opa_t bg_opa; int32_t radius; } lv_draw_rect_dsc_t;
typedef struct { lv_point_precise_t p1, p2; lv_color_t color; int32_t width; lv_opa_t opa; uint8_t round_start, round_end; } lv_draw_line_dsc_t;
static inline lv_color_t lv_color_hex(uint32_t c) { lv_color_t r = { c }; return r; }
static inline int32_t lv_area_get_width(const lv_area_t *a) { return a->x2 - a->x1 + 1; }
static inline int32_t lv_area_get_height(const lv_area_t *a) { return a->y2 - a->y1 + 1; }
lv_obj_t *lv_obj_create(lv_obj_t *parent);
void lv_obj_remove_style_all(lv_obj_t *o);
void lv_obj_add_flag(lv_obj_t *o, uint32_t f);
void lv_obj_remove_flag(lv_obj_t *o, uint32_t f);
void lv_obj_set_align(lv_obj_t *o, int a);
void lv_obj_set_pos(lv_obj_t *o, int32_t x, int32_t y);
void lv_obj_set_size(lv_obj_t *o, int32_t w, int32_t h);
void lv_obj_set_style_radius(lv_obj_t *o, int32_t r, int sel);
void lv_obj_set_style_bg_color(lv_obj_t *o, lv_color_t c, int sel);
void lv_obj_set_style_bg_opa(lv_obj_t *o, lv_opa_t a, int sel);
void lv_obj_set_style_border_width(lv_obj_t *o, int32_t w, int sel);
void lv_obj_set_style_border_color(lv_obj_t *o, lv_color_t c, int sel);
void lv_obj_move_foreground(lv_obj_t *o);
void lv_obj_invalidate(lv_obj_t *o);
void lv_obj_get_coords(const lv_obj_t *o, lv_area_t *a);
void lv_obj_add_event_cb(lv_obj_t *o, lv_event_cb_t cb, int code, void *user);
lv_obj_t *lv_event_get_target(lv_event_t *e);
void *lv_event_get_user_data(lv_event_t *e);
lv_layer_t *lv_event_get_layer(lv_event_t *e);
void lv_draw_triangle_dsc_init(lv_draw_triangle_dsc_t *d);
void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t *d);
void lv_draw_line_dsc_init(lv_draw_line_dsc_t *d);
void lv_draw_triangle(lv_layer_t *l, const lv_draw_triangle_dsc_t *d);
void lv_draw_rect(lv_layer_t *l, const lv_draw_rect_dsc_t *d, const lv_area_t *a);
void lv_draw_line(lv_layer_t *l, const lv_draw_line_dsc_t *d);
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *user);
uint32_t lv_tick_get(void);
