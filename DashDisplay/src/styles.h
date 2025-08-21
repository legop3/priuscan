#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: sharp square
lv_style_t *get_style_sharp_square_MAIN_DEFAULT();
lv_style_t *get_style_sharp_square_INDICATOR_DEFAULT();
void add_style_sharp_square(lv_obj_t *obj);
void remove_style_sharp_square(lv_obj_t *obj);

// Style: text readable
lv_style_t *get_style_text_readable_MAIN_DEFAULT();
void add_style_text_readable(lv_obj_t *obj);
void remove_style_text_readable(lv_obj_t *obj);



#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/