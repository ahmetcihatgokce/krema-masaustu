#pragma once
#include <stdint.h>

enum { PET_WIDTH = 282, PET_HEIGHT = 262 };
typedef struct { int left, top, right, bottom; } WorkArea;
typedef enum { AT_DESK, HISSING, ASLEEP, WALKING } Pose;
typedef enum { NO_TOUCH, HEAD_TOUCH, FLANK_TOUCH, OTHER_TOUCH } TouchZone;
typedef enum { QUIET, PLAY_STROKE, PLAY_HISS, PLAY_SLEEP, STOP_AUDIO } SoundCue;
typedef struct {
    double last_stroke, next_stroke_sound;
    float affection, lean, stroke_distance;
    TouchZone contact;
    SoundCue cue;
    double now, last_tick, last_input, last_mouse, last_key, last_click, last_hiss, hold_position_until;
    float x, y, dpi_scale, walk_phase, facing, gaze_x, gaze_y;
    float mouse_x, mouse_y, mouse_target_x, mouse_target_y, tap_x;
    int cursor_x, cursor_y, have_pointer, keycap, mouse_button, held_buttons;
    int following, walking;
    Pose pose;
} Pet;

void pet_init(Pet *pet, double now, WorkArea area, float scale);
void pet_home(Pet *pet, WorkArea area);
void pet_wake(Pet *pet, double now);
void pet_key(Pet *pet, int scan_code, int extended, double now);
void pet_button(Pet *pet, int button, int pressed, double now);
void pet_hiss(Pet *pet, double now);
void pet_touch(Pet *pet, TouchZone zone, float dx, float dy, double now, int tapped);
SoundCue pet_take_sound(Pet *pet);
void pet_step(Pet *pet, double now, int cursor_x, int cursor_y, WorkArea area, int hold_position);
void pet_render(const Pet *pet, uint32_t *premultiplied_bgra, uint8_t *touch_map, int width, int height);
