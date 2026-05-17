#ifndef ZMK_RGB_PLUS_H
#define ZMK_RGB_PLUS_H

#include "dt-bindings/zmk/rgb_plus.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>

/* Behavior commands mapped to keymap cells */
#define RGB_PLUS_CMD_EFF_NEXT eff_next
#define RGB_PLUS_CMD_EFF_PREV eff_prev
#define RGB_PLUS_CMD_REAC_TOG reac_tog
#define RGB_PLUS_CMD_SPD_INC spd_inc
#define RGB_PLUS_CMD_SPD_DEC spd_dec
#define RGB_PLUS_CMD_BRI_INC bri_inc
#define RGB_PLUS_CMD_BRI_DEC bri_dec

/* Available custom effects */
enum zmk_rgb_plus_effect {
  RGB_PLUS_EFF_AURORA = 0,
  RGB_PLUS_EFF_FIRE,
  RGB_PLUS_EFF_STAR,
  RGB_PLUS_EFF_RAINBOW,
  RGB_PLUS_EFF_RIPPLE,
  RGB_PLUS_EFF_HEATMAP,
  RGB_PLUS_EFF_COUNT
};

struct point_2d {
  float x;
  float y;
};

struct ripple {
  float x;
  float y;
  int64_t start_time;
  bool active;
};

struct key_heat {
  float heat;
  int64_t last_hit;
};

#endif /* ZMK_RGB_PLUS_H */
