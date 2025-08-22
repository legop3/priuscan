#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: sharp square
//

void init_style_sharp_square_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_radius(style, 0);
};

lv_style_t *get_style_sharp_square_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_sharp_square_MAIN_DEFAULT(style);
    }
    return style;
};

void init_style_sharp_square_INDICATOR_DEFAULT(lv_style_t *style) {
    lv_style_set_radius(style, 0);
    lv_style_set_border_color(style, lv_color_hex(0xffffffff));
    lv_style_set_border_width(style, 5);
    lv_style_set_border_side(style, LV_BORDER_SIDE_BOTTOM|LV_BORDER_SIDE_TOP);
};

lv_style_t *get_style_sharp_square_INDICATOR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_sharp_square_INDICATOR_DEFAULT(style);
    }
    return style;
};

void add_style_sharp_square(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_sharp_square_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_sharp_square_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

void remove_style_sharp_square(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_sharp_square_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_sharp_square_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

//
// Style: text readable
//

void init_style_text_readable_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_color(style, lv_color_hex(0xffffffff));
    lv_style_set_bg_color(style, lv_color_hex(0xff000000));
    lv_style_set_bg_opa(style, 50);
    lv_style_set_pad_left(style, 4);
    lv_style_set_pad_right(style, 4);
    lv_style_set_radius(style, 10);
};

lv_style_t *get_style_text_readable_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_text_readable_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_text_readable(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_text_readable_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_text_readable(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_text_readable_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_sharp_square,
        add_style_text_readable,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_sharp_square,
        remove_style_text_readable,
    };
    remove_style_funcs[styleIndex](obj);
}

