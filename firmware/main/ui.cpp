#include "ui.hpp"
#include "bsp/esp-bsp.h"
#include <cmath>
#include <cstring>

namespace {
struct DisplayGuard {
    explicit DisplayGuard(uint32_t timeout = 100) : locked(bsp_display_lock(timeout) == ESP_OK) {}
    ~DisplayGuard() { if (locked) bsp_display_unlock(); }
    bool locked;
};

void draw_text_outline(lv_event_t *event)
{
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    const char *text = lv_label_get_text(obj);
    if (!text || !*text) return;

    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.text = text;
    descriptor.color = lv_color_black();
    lv_obj_init_draw_label_dsc(obj, LV_PART_MAIN, &descriptor);
    descriptor.color = lv_color_black();
    lv_bidi_calculate_align(&descriptor.align, &descriptor.bidi_dir, text);

    lv_area_t coords;
    lv_obj_get_content_coords(obj, &coords);
    lv_layer_t *layer = lv_event_get_layer(event);
    descriptor.base.layer = layer;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            lv_area_t outline_coords = coords;
            lv_area_move(&outline_coords, x, y);
            lv_draw_label(layer, &descriptor, &outline_coords);
        }
    }
}

void add_text_outline(lv_obj_t *label)
{
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(label, draw_text_outline, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
}
}

void stop_motion(lv_obj_t *image)
{
    lv_anim_delete(image, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_x));
    lv_anim_delete(image, reinterpret_cast<lv_anim_exec_xcb_t>(lv_obj_set_y));
}

void animate_axis(lv_obj_t *image, bool horizontal, int32_t from, int32_t to, uint32_t duration)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, image);
    lv_anim_set_exec_cb(&animation, reinterpret_cast<lv_anim_exec_xcb_t>(
        horizontal ? lv_obj_set_x : lv_obj_set_y));
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_reverse_duration(&animation, duration);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

LV_FONT_DECLARE(font_smiley);

bool Ui::begin()
{
    font_ = &font_smiley;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    image_ = lv_image_create(screen);
    lv_obj_set_size(image_, 498, 498);
    lv_obj_center(image_);
    lv_obj_move_background(image_);

    active_glow_ = lv_image_create(screen);
#if CONFIG_SAGE_ROLE_SUN
    lv_image_set_src(active_glow_, &active_glow_square);
#else
    lv_image_set_src(active_glow_, &active_glow_circle);
#endif
    lv_obj_center(active_glow_);
    lv_obj_set_hidden(active_glow_, true);

    slogan_ = lv_label_create(screen);
    lv_obj_set_style_text_font(slogan_, font_, LV_PART_MAIN);
    lv_obj_set_style_text_color(slogan_, lv_color_hex(0xFFD36A), LV_PART_MAIN);
    lv_obj_align(slogan_, LV_ALIGN_BOTTOM_MID, 0, -110);
    add_text_outline(slogan_);

    status_ = lv_label_create(screen);
    lv_obj_set_width(status_, 400);
    lv_obj_set_style_text_font(status_, font_, LV_PART_MAIN);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_, lv_color_hex(0xFFD36A), LV_PART_MAIN);
    lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, 82);
    add_text_outline(status_);
    lv_obj_set_hidden(status_, true);

    subtitle_ = lv_label_create(screen);
    lv_obj_set_width(subtitle_, 390);
    lv_label_set_long_mode(subtitle_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(subtitle_, font_, LV_PART_MAIN);
    lv_obj_set_style_text_align(subtitle_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(subtitle_, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(subtitle_, LV_ALIGN_BOTTOM_MID, 0, -70);
    add_text_outline(subtitle_);
    lv_obj_set_hidden(subtitle_, true);

    hint_ = lv_label_create(screen);
    lv_obj_set_width(hint_, 390);
    lv_obj_set_style_text_font(hint_, font_, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_, lv_color_hex(0xC7C7C7), LV_PART_MAIN);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -26);
    add_text_outline(hint_);
    lv_obj_set_hidden(hint_, true);
    return true;
}

void Ui::person(const char *speaker)
{
    int id = 0;
    const lv_image_dsc_t *face = &face_shaolan;
    if (speaker && std::strcmp(speaker, "sun") == 0) {
        id = 1;
        face = &face_sun;
    }
    if (speaker_id_ == id) return;
    speaker_id_ = id;
    lv_image_set_src(image_, face);
}

void Ui::idle(int person_index)
{
    DisplayGuard guard;
    if (!guard.locked) return;
    idle_person_ = person_index;
#if CONFIG_SAGE_ROLE_SUN
    person("sun");
#else
    person("shaolan");
#endif
    lv_obj_set_hidden(slogan_, true);
    lv_label_set_text(slogan_, "与智者对话");
    lv_label_set_text(status_, "");
    lv_label_set_text(hint_, "");
    lv_obj_set_hidden(active_glow_, true);
    stop_motion(image_);
    motion_active_ = false;
    lv_obj_set_style_transform_scale_y(image_, 256, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(image_, 0, LV_PART_MAIN);
    lv_obj_set_x(image_, 0);
    lv_obj_set_y(image_, 0);
}

void Ui::recording(int seconds)
{
    DisplayGuard guard;
    if (!guard.locked) return;
    lv_obj_set_hidden(slogan_, true);
    lv_label_set_text_fmt(status_, "录音中 %02d s", seconds);
    lv_label_set_text(hint_, "最长 20 秒");
}

void Ui::status(const char *speaker, const char *message, const char *text)
{
    DisplayGuard guard;
    if (!guard.locked) return;
    lv_obj_set_hidden(slogan_, true);
    person(speaker);
    lv_label_set_text(status_, message);
    lv_label_set_text(hint_, "按 PWR 停止");
    lv_obj_set_hidden(active_glow_, true);
    stop_motion(image_);
    motion_active_ = false;
    lv_obj_set_style_transform_scale_y(image_, 256, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(image_, 0, LV_PART_MAIN);
    lv_obj_set_x(image_, 0);
    lv_obj_set_y(image_, 0);
}

void Ui::playing(const char *speaker, const char *text, int frame)
{
    if (motion_active_) return;
    DisplayGuard guard;
    if (!guard.locked) return;
    lv_obj_set_hidden(slogan_, true);
    lv_obj_set_hidden(active_glow_, false);
    person(speaker);
    lv_label_set_text(status_, "正在接话");
    lv_label_set_text(hint_, "按 PWR 停止");
    animate_axis(image_, true, -5, 5, 240);
    animate_axis(image_, false, 0, -7, 240);
    motion_active_ = true;
}

void Ui::error(const char *message)
{
    DisplayGuard guard;
    if (!guard.locked) return;
    lv_obj_set_hidden(slogan_, true);
    lv_obj_set_hidden(active_glow_, true);
    stop_motion(image_);
    motion_active_ = false;
    lv_label_set_text(status_, "出错了");
    lv_label_set_text(subtitle_, message);
    lv_label_set_text(hint_, "按 PWR 重新说话");
}
