#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;
uint32_t active_theme_index = 0;

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // kw_watts_bar
            lv_obj_t *obj = lv_bar_create(parent_obj);
            objects.kw_watts_bar = obj;
            lv_obj_set_pos(obj, 656, 0);
            lv_obj_set_size(obj, 144, 480);
            lv_bar_set_range(obj, -20000, 20000);
            lv_bar_set_mode(obj, LV_BAR_MODE_RANGE);
            lv_bar_set_value(obj, 20000, LV_ANIM_OFF);
            lv_bar_set_start_value(obj, -11000, LV_ANIM_OFF);
            add_style_sharp_square(obj);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff00ff26), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff00ff26), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // kw_label
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.kw_label = obj;
                    lv_obj_set_pos(obj, 0, 8);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "15\nkW");
                }
            }
        }
        {
            // rpm_bar
            lv_obj_t *obj = lv_bar_create(parent_obj);
            objects.rpm_bar = obj;
            lv_obj_set_pos(obj, 512, 0);
            lv_obj_set_size(obj, 144, 480);
            lv_bar_set_range(obj, 0, 5500);
            lv_bar_set_value(obj, 1000, LV_ANIM_OFF);
            add_style_sharp_square(obj);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffa000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffa000), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // rpm_label
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.rpm_label = obj;
                    lv_obj_set_pos(obj, 0, 8);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "1500\nRPM");
                }
            }
        }
        {
            // battery info panel
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.battery_info_panel = obj;
            lv_obj_set_pos(obj, 8, 70);
            lv_obj_set_size(obj, 228, 214);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_CLICK_FOCUSABLE|LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_PRESS_LOCK|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_WITH_ARROW|LV_OBJ_FLAG_SNAPPABLE);
            add_style_panel_crisp(obj);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff00ff1b), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // battery soc
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.battery_soc = obj;
                    lv_obj_set_pos(obj, 0, -29);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "990%\nSoC");
                }
                {
                    // battery temp
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.battery_temp = obj;
                    lv_obj_set_pos(obj, 0, 57);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffff7d7d), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "909°");
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 13, -16);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_label_set_text(obj, "Average Battery Info");
                }
            }
        }
        {
            // battery fan info panel
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.battery_fan_info_panel = obj;
            lv_obj_set_pos(obj, 243, 70);
            lv_obj_set_size(obj, 261, 214);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_CLICK_FOCUSABLE|LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_PRESS_LOCK|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_WITH_ARROW|LV_OBJ_FLAG_SNAPPABLE);
            add_style_panel_crisp(obj);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff00ff1b), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // battery intake temp
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.battery_intake_temp = obj;
                    lv_obj_set_pos(obj, 0, -29);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "999°\nIntake");
                }
                {
                    // battery fan speed
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.battery_fan_speed = obj;
                    lv_obj_set_pos(obj, 6, 5);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(obj, LV_ALIGN_BOTTOM_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "S: 6");
                }
                {
                    // battery fan control
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    objects.battery_fan_control = obj;
                    lv_obj_set_pos(obj, -5, 127);
                    lv_obj_set_size(obj, 129, 50);
                    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    add_style_panel_crisp(obj);
                    lv_obj_set_style_radius(obj, 1255, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_clip_corner(obj, true, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_color(obj, lv_color_hex(0xff2f3237), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffff0000), LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // fan control label
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.fan_control_label = obj;
                            lv_obj_set_pos(obj, -3, -18);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            add_style_text_readable(obj);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "Control\nDisabled");
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 44, -16);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_label_set_text(obj, "Battery Fan Stats");
                }
            }
        }
        {
            // battery temps
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.battery_temps = obj;
            lv_obj_set_pos(obj, 8, 292);
            lv_obj_set_size(obj, 228, 180);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            add_style_panel_crisp(obj);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff618cd6), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 35, -12);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_label_set_text(obj, "Battery Temps");
                }
                {
                    // bt1
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.bt1 = obj;
                    lv_obj_set_pos(obj, -12, 7);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_44, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "17.3°");
                }
                {
                    // bt2
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.bt2 = obj;
                    lv_obj_set_pos(obj, -12, 57);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_44, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "1.3°");
                }
                {
                    // bt3
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.bt3 = obj;
                    lv_obj_set_pos(obj, -12, 108);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_44, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "207.3°");
                }
            }
        }
        {
            // battery stats
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.battery_stats = obj;
            lv_obj_set_pos(obj, 243, 292);
            lv_obj_set_size(obj, 261, 180);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            add_style_panel_crisp(obj);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffcf97ce), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 59, -12);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_label_set_text(obj, "Battery Stats");
                }
                {
                    // battery voltage
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.battery_voltage = obj;
                    lv_obj_set_pos(obj, 1, -26);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "888v");
                }
                {
                    // battery amperage
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.battery_amperage = obj;
                    lv_obj_set_pos(obj, 1, 38);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "888A");
                }
            }
        }
        {
            // ebar info
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.ebar_info = obj;
            lv_obj_set_pos(obj, 8, 5);
            lv_obj_set_size(obj, 496, 60);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            add_style_panel_crisp(obj);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffff00fb), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_clip_corner(obj, true, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // ebar bar
                    lv_obj_t *obj = lv_bar_create(parent_obj);
                    objects.ebar_bar = obj;
                    lv_obj_set_pos(obj, -20, -20);
                    lv_obj_set_size(obj, 496, 60);
                    lv_bar_set_value(obj, 25, LV_ANIM_OFF);
                    add_style_sharp_square(obj);
                    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_NONE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffcaff00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
                }
                {
                    // ebar label
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.ebar_label = obj;
                    lv_obj_set_pos(obj, -13, -16);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Ebar: -100");
                }
                {
                    // energy drain
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.energy_drain = obj;
                    lv_obj_set_pos(obj, 244, -16);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_text_readable(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Mode: 10");
                }
            }
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
}



typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen_funcs[screenId - 1]();
}

void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    create_screen_main();
}
