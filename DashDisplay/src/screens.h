#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *kw_watts_bar;
    lv_obj_t *kw_label;
    lv_obj_t *rpm_bar;
    lv_obj_t *rpm_label;
    lv_obj_t *battery_info_panel;
    lv_obj_t *battery_soc;
    lv_obj_t *battery_temp;
    lv_obj_t *battery_fan_info_panel;
    lv_obj_t *battery_intake_temp;
    lv_obj_t *battery_fan_speed;
    lv_obj_t *battery_fan_control;
    lv_obj_t *numbers_panel;
    lv_obj_t *battery_temps;
    lv_obj_t *bt1;
    lv_obj_t *bt2;
    lv_obj_t *bt3;
    lv_obj_t *battery_stats;
    lv_obj_t *battery_voltage;
    lv_obj_t *battery_amperage;
    lv_obj_t *ebar_testing;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/