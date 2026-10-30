#pragma once
#include "app_state.hpp"
#include "lvgl.h"

class Ui {
public:
    bool begin();
    void idle(int person);
    void recording(int seconds);
    void status(const char *speaker, const char *status, const char *text = "");
    void playing(const char *speaker, const char *text, int frame);
    void error(const char *message);

private:
    void person(const char *speaker);
    lv_obj_t *image_ = nullptr;
    lv_obj_t *active_glow_ = nullptr;
    lv_obj_t *slogan_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *subtitle_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    const lv_font_t *font_ = nullptr;
    int idle_person_ = -1;
    int speaker_id_ = -1;
    bool motion_active_ = false;
};

extern const lv_image_dsc_t active_glow_circle;
extern const lv_image_dsc_t active_glow_square;
extern const lv_image_dsc_t face_shaolan;
extern const lv_image_dsc_t face_sun;
