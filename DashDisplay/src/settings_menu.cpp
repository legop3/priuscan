#include "settings_menu.h"
#include "../../shared/dash_menu_protocol.h"
#include "../../shared/dash_settings.h"
#include <Preferences.h>
#include <lvgl.h>
namespace dash_menu {
namespace {
using namespace dash_settings;
Preferences preferences;
constexpr unsigned VISIBLE_ROWS=4;
lv_obj_t *panel=nullptr, *rows[VISIBLE_ROWS]{}, *names[VISIBLE_ROWS]{},
    *numbers[VISIBLE_ROWS]{}, *hint=nullptr, *status=nullptr;
uint8_t values[COUNT]{}, saved[COUNT]{}, state=0;
uint32_t lastMessage=0;
int applied[2]={-1,-1};
bool storageReady=false;
void (*outputs[2])(uint8_t){};
void save() {
    for(unsigned i=0;i<COUNT;++i) if(values[i]!=saved[i] && storageReady) {
        if(preferences.putUChar(ITEMS[i].key,values[i])==1) saved[i]=values[i];
    }
}
void close() {
    state=0; save();
    lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
}
void refresh() {
    using namespace dash_menu_wire;
    const unsigned item=selected(state);
    const unsigned first=item>=VISIBLE_ROWS ? item-VISIBLE_ROWS+1 : 0;
    for(unsigned row=0;row<VISIBLE_ROWS;++row) {
        const unsigned i=first+row;
        if(i>=COUNT) { lv_obj_add_flag(rows[row],LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(rows[row],LV_OBJ_FLAG_HIDDEN);
        const bool highlighted=i==item;
        lv_label_set_text(names[row],ITEMS[i].label);
        lv_label_set_text_fmt(numbers[row],"%u%%",static_cast<unsigned>(values[i]));
        lv_obj_set_style_bg_color(rows[row],lv_color_hex(highlighted ? 0x193B49 : 0x101820),0);
        lv_obj_set_style_border_width(rows[row],highlighted && (state & EDITING) ? 2 : 0,0);
        lv_obj_set_style_text_color(numbers[row],lv_color_hex(highlighted ? 0x47E5FF : 0xD8DFE8),0);
    }
    lv_label_set_text(status,(state & EDITING) ? "ADJUSTING" : "CHOOSE SETTING");
    lv_label_set_text(hint,(state & EDITING) ?
        "UP / DOWN  adjust     SELECT / BACK  done" :
        "UP / DOWN  choose     SELECT  edit     BACK  save & close");
}
}
void begin(void (*writeScreen)(uint8_t), void (*writeStrip)(uint8_t)) {
    outputs[0]=writeScreen; outputs[1]=writeStrip;
    storageReady=preferences.begin("dash-bright",false);
    for(unsigned i=0;i<COUNT;++i) {
        const uint8_t value=storageReady ?
            preferences.getUChar(ITEMS[i].key,ITEMS[i].defaultValue) : ITEMS[i].defaultValue;
        values[i]=value>=ITEMS[i].minimum && value<=ITEMS[i].maximum ? value : ITEMS[i].defaultValue;
        saved[i]=values[i];
    }
    panel=lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel,640,310);
    lv_obj_align(panel,LV_ALIGN_CENTER,0,0);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel,lv_color_hex(0x101820),0);
    lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(panel,lv_color_hex(0x26758A),0);
    lv_obj_set_style_border_width(panel,2,0);
    lv_obj_set_style_radius(panel,10,0);
    lv_obj_set_style_pad_all(panel,18,0);
    auto title=lv_label_create(panel);
    lv_label_set_text(title,"SETTINGS");
    lv_obj_set_style_text_color(title,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0);
    status=lv_label_create(panel);
    lv_obj_align(status,LV_ALIGN_TOP_RIGHT,0,5);
    lv_obj_set_style_text_font(status,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(status,lv_color_hex(0x8BABB8),0);
    for(unsigned i=0;i<VISIBLE_ROWS;++i) {
        rows[i]=lv_obj_create(panel);
        lv_obj_remove_style_all(rows[i]);
        lv_obj_set_pos(rows[i],0,44+i*44);
        lv_obj_set_size(rows[i],600,40);
        lv_obj_clear_flag(rows[i],LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(rows[i],LV_OPA_COVER,0);
        lv_obj_set_style_radius(rows[i],5,0);
        lv_obj_set_style_border_color(rows[i],lv_color_hex(0x47E5FF),0);
        names[i]=lv_label_create(rows[i]);
        lv_obj_set_style_text_font(names[i],&lv_font_montserrat_24,0);
        lv_obj_set_style_text_color(names[i],lv_color_hex(0xD8DFE8),0);
        lv_obj_align(names[i],LV_ALIGN_LEFT_MID,12,0);
        numbers[i]=lv_label_create(rows[i]);
        lv_obj_set_style_text_font(numbers[i],&lv_font_montserrat_24,0);
        lv_obj_align(numbers[i],LV_ALIGN_RIGHT_MID,-12,0);
    }
    hint=lv_label_create(panel);
    lv_obj_set_pos(hint,0,240);
    lv_obj_set_style_text_font(hint,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(hint,lv_color_hex(0xA0AAB8),0);
    refresh(); lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
}
void receive(uint8_t next, int8_t adjustment, uint32_t now) {
    using namespace dash_menu_wire;
    if(selected(next)>=COUNT || (!(next & OPEN) && next) ||
       (adjustment && (!(next & OPEN) || !(next & EDITING))) ||
       (adjustment!=0 && adjustment!=1 && adjustment!=-1)) return;
    lastMessage=now;
    const uint8_t previous=state;
    if(!(next & OPEN)) { if(state & OPEN) close(); return; }
    state=next;
    bool changed=previous!=state;
    if(adjustment) {
        const unsigned i=selected(state);
        const auto& item=ITEMS[i];
        int value=values[i]+adjustment*item.step;
        if(value<item.minimum)value=item.minimum;
        if(value>item.maximum)value=item.maximum;
        changed |= values[i]!=value;
        values[i]=value;
    }
    if(!(previous & OPEN)) lv_obj_clear_flag(panel,LV_OBJ_FLAG_HIDDEN);
    if(changed) refresh();
}
bool stripPreview() {
    return (state & dash_menu_wire::EDITING) &&
        ITEMS[dash_menu_wire::selected(state)].output==Output::Strip;
}
void tick(uint32_t now, bool dim, bool off) {
    using namespace dash_menu_wire;
    if((state & OPEN) && (off || now-lastMessage>=LINK_TIMEOUT_MS)) close();
    const unsigned selection=selected(state);
    for(unsigned output=0;output<2;++output) {
        const bool preview=(state & EDITING) && static_cast<unsigned>(ITEMS[selection].output)==output;
        const bool night=preview ? ITEMS[selection].night : dim;
        int target=0;
        for(unsigned i=0;i<COUNT;++i)
            if(static_cast<unsigned>(ITEMS[i].output)==output && ITEMS[i].night==night) target=values[i];
        if(off)target=0;
        if(target!=applied[output] && outputs[output]) {
            outputs[output](target); applied[output]=target;
        }
    }
}
}
