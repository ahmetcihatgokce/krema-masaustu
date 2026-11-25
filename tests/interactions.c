#include <assert.h>
#include <stdlib.h>
#include "../src/pet.h"

static void check_scale(int percent)
{
    float scale = percent / 100.0f;
    int width = (int)(PET_WIDTH * scale + .5f), height = (int)(PET_HEIGHT * scale + .5f);
    uint32_t *pixels = calloc((size_t)width * height, sizeof *pixels);
    uint8_t *zones = calloc((size_t)width * height, sizeof *zones);
    assert(pixels && zones);
    WorkArea area = {0, 0, 1920, 1080};
    Pet pet;
    pet_init(&pet, 0, area, scale);
    pet_step(&pet, .02, 10, 10, area, 0);
    pet_render(&pet, pixels, zones, width, height);
    #define ZONE(x,y) ((TouchZone)zones[(int)((y)*scale)*width + (int)((x)*scale)])
    assert(ZONE(150,45) == HEAD_TOUCH && ZONE(170,45) == HEAD_TOUCH);
    assert(ZONE(115,155) == FLANK_TOUCH && ZONE(232,155) == FLANK_TOUCH);
    assert(ZONE(166,145) == OTHER_TOUCH && ZONE(1,1) == NO_TOUCH && ZONE(50,225) == NO_TOUCH);

    pet_touch(&pet, ZONE(150,45), 0, 0, .10, 0);
    assert(pet_take_sound(&pet) == QUIET);
    pet_touch(&pet, ZONE(170,45), 20, 0, .20, 0);
    assert(pet_take_sound(&pet) == PLAY_STROKE);
    for (int i=1; i<=20; i++) {
        double now = .20 + i * .05;
        pet_touch(&pet, HEAD_TOUCH, 8, 0, now, 0);
        pet_step(&pet, now, 10, 10, area, 0);
        assert(pet_take_sound(&pet) == QUIET);
    }
    assert(pet.affection > .9f);
    pet_touch(&pet, ZONE(115,155), 1, 0, 1.30, 0);
    pet_step(&pet, 1.31, 10, 10, area, 0);
    assert(pet.pose == HISSING && pet_take_sound(&pet) == PLAY_HISS);
    pet_touch(&pet, FLANK_TOUCH, 2, 0, 3.0, 0);
    assert(pet_take_sound(&pet) == QUIET);
    pet_touch(&pet, NO_TOUCH, 2, 0, 3.05, 0);
    pet_touch(&pet, ZONE(232,155), 2, 0, 3.1, 0);
    assert(pet_take_sound(&pet) == PLAY_HISS);
    pet_step(&pet, 28.2, 10, 10, area, 0);
    assert(pet.pose == ASLEEP && pet_take_sound(&pet) == PLAY_SLEEP);
    pet_step(&pet, 33.2, 10, 10, area, 0);
    assert(pet_take_sound(&pet) == QUIET);
    pet_step(&pet, 33.3, 11, 10, area, 0);
    assert(pet.pose == AT_DESK && pet_take_sound(&pet) == STOP_AUDIO);
    pet_touch(&pet, HEAD_TOUCH, 0, 0, 35.0, 1);
    assert(pet_take_sound(&pet) == PLAY_STROKE);
    free(pixels);
    free(zones);
    #undef ZONE
}

int main(void)
{
    check_scale(100);
    check_scale(150);
    check_scale(200);
    return 0;
}
