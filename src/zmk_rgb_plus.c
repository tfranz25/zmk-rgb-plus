#define DT_DRV_COMPAT zmk_behavior_rgb_plus

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/usb.h>
#include <zmk/behavior.h>
#include <drivers/behavior.h>
#include <math.h>

#include "zmk_rgb_plus.h"

#define M_PI 3.14159265358979323846f

/* Retrieve standard ZMK underglow device */
#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#if DT_NODE_HAS_PROP(STRIP_NODE, chain_length)
  #define LED_COUNT DT_PROP(STRIP_NODE, chain_length)
#else
  #define LED_COUNT 20
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ext_power_generic)
static const struct gpio_dt_spec ext_power_gpio = GPIO_DT_SPEC_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zmk_ext_power_generic), control_gpios);
#endif

/* Retrieve physical layout coordinates at compile-time */
#if DT_HAS_CHOSEN(zmk_physical_layout) && DT_NODE_HAS_PROP(DT_CHOSEN(zmk_physical_layout), keys)
    #define LAYOUT_NODE DT_CHOSEN(zmk_physical_layout)
    #define KEY_COORD_ENTRY(node_id, prop, idx) \
        { \
            .x = (float)DT_PHA_BY_IDX(node_id, prop, idx, x) / 100.0f, \
            .y = (float)DT_PHA_BY_IDX(node_id, prop, idx, y) / 100.0f  \
        },
    static const struct point_2d key_positions[] = {
        DT_FOREACH_PROP_ELEM(LAYOUT_NODE, keys, KEY_COORD_ENTRY)
    };
    #define KEY_COUNT ARRAY_SIZE(key_positions)
#else
    /* Fallback standard 4x5 TibbyPad layout if layout doesn't specify physical keys */
    static const struct point_2d key_positions[] = {
        {0,0}, {1,0}, {2,0}, {3,0},
        {0,1}, {1,1}, {2,1}, {3,1},
        {0,2}, {1,2}, {2,2}, {3,2},
        {0,3}, {1,3}, {2,3}, {3,3},
        {0,4}, {1,4}, {2,4}, {3,4}
    };
    #define KEY_COUNT 20
#endif

/* Global runtime states */
static struct led_rgb led_buffer[LED_COUNT];
static struct point_2d led_positions[LED_COUNT];
static bool coords_initialized = false;

/* Active effect parameters */
static enum zmk_rgb_plus_effect active_effect = RGB_PLUS_EFF_AURORA;
static bool reactive_overlays_enabled = true;
static int speed_multiplier = 100; // default speed scale in percent
static int brightness_multiplier = 100; // Master brightness scaling in percent

/* Reactive effect track lists */
#define MAX_RIPPLES 6
static struct ripple ripples[MAX_RIPPLES];
static uint8_t ripple_head = 0;
static struct key_heat heatmap[KEY_COUNT];

/* Delayable periodic work for frame rendering */
static struct k_work_delayable rgb_work;

/* High-efficiency HSV to RGB converter */
static struct led_rgb hsv_to_rgb(float h, float s, float v) {
    struct led_rgb rgb;
    float r = 0, g = 0, b = 0;
    
    // Clamp values
    if (h < 0.0f) h = 0.0f;
    if (h >= 360.0f) h = fmodf(h, 360.0f);
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    if (s == 0.0f) {
        r = g = b = v;
    } else {
        float h_sector = h / 60.0f;
        int i = (int)floorf(h_sector);
        float f = h_sector - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - s * f);
        float t = v * (1.0f - s * (1.0f - f));
        
        switch (i % 6) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            case 5: r = v; g = p; b = q; break;
        }
    }
    
    rgb.r = (uint8_t)(r * 255.0f);
    rgb.g = (uint8_t)(g * 255.0f);
    rgb.b = (uint8_t)(b * 255.0f);
    return rgb;
}

/* Linear spatial interpolation helper */
static float dist_2d(float x1, float y1, float x2, float y2) {
    return sqrtf((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

/* Setup LED positions spatially */
static void init_led_coordinates(void) {
    if (coords_initialized) return;

    /* 1. If explicit LED coordinates node is defined in DT, use it */
#if DT_HAS_COMPAT_STATUS_OKAY(zmk_rgb_plus_layout)
    #define DT_LAYOUT_NODE DT_INST(0, zmk_rgb_plus_layout)
    #if DT_NODE_HAS_PROP(DT_LAYOUT_NODE, led_coordinates)
    static const int32_t raw_coords[] = {
        DT_FOREACH_PROP_ELEM(DT_LAYOUT_NODE, led_coordinates, DT_PROP_BY_IDX)
    };
    int num_coords = ARRAY_SIZE(raw_coords) / 2;
    int to_copy = (num_coords < LED_COUNT) ? num_coords : LED_COUNT;
    for (int i = 0; i < to_copy; i++) {
        led_positions[i].x = (float)raw_coords[2 * i] / 100.0f;
        led_positions[i].y = (float)raw_coords[2 * i + 1] / 100.0f;
    }
    coords_initialized = true;
    return;
    #endif
#endif

    /* 2. Default fallback mapping strategies */
    if (LED_COUNT == KEY_COUNT) {
        /* Per-Key 1-to-1 mode */
        for (int i = 0; i < LED_COUNT; i++) {
            led_positions[i] = key_positions[i];
        }
    } else {
        /* Auto-perimeter/Bounding box distribution */
        float min_x = key_positions[0].x, max_x = key_positions[0].x;
        float min_y = key_positions[0].y, max_y = key_positions[0].y;
        
        for (int i = 1; i < KEY_COUNT; i++) {
            if (key_positions[i].x < min_x) min_x = key_positions[i].x;
            if (key_positions[i].x > max_x) max_x = key_positions[i].x;
            if (key_positions[i].y < min_y) min_y = key_positions[i].y;
            if (key_positions[i].y > max_y) max_y = key_positions[i].y;
        }

        float width = max_x - min_x;
        float height = max_y - min_y;
        float perimeter = 2.0f * (width + height);
        float step = perimeter / (float)LED_COUNT;

        for (int i = 0; i < LED_COUNT; i++) {
            float dist = step * (float)i;
            if (dist < width) {
                // Top border
                led_positions[i].x = min_x + dist;
                led_positions[i].y = min_y;
            } else if (dist < width + height) {
                // Right border
                led_positions[i].x = max_x;
                led_positions[i].y = min_y + (dist - width);
            } else if (dist < 2.0f * width + height) {
                // Bottom border
                led_positions[i].x = max_x - (dist - (width + height));
                led_positions[i].y = max_y;
            } else {
                // Left border
                led_positions[i].x = min_x;
                led_positions[i].y = max_y - (dist - (2.0f * width + height));
            }
        }
    }
    coords_initialized = true;
}

/* Event handler for key presses */
static int keypress_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL || ev->position >= KEY_COUNT) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    int64_t now = k_uptime_get();

    /* 1. Add key heatmap triggers */
    if (ev->state) { // Key down
        heatmap[ev->position].heat += 0.35f;
        if (heatmap[ev->position].heat > 1.0f) {
            heatmap[ev->position].heat = 1.0f;
        }
        heatmap[ev->position].last_hit = now;

        /* 2. Spawn a splash ripple */
        if (reactive_overlays_enabled || active_effect == RGB_PLUS_EFF_RIPPLE) {
            ripples[ripple_head].x = key_positions[ev->position].x;
            ripples[ripple_head].y = key_positions[ev->position].y;
            ripples[ripple_head].start_time = now;
            ripples[ripple_head].active = true;
            ripple_head = (ripple_head + 1) % MAX_RIPPLES;
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zmk_rgb_plus, keypress_listener);
ZMK_SUBSCRIPTION(zmk_rgb_plus, zmk_position_state_changed);

/* Effect Renderers */

static struct led_rgb render_aurora(int i, int64_t time_ms) {
    float led_x = led_positions[i].x;
    float led_y = led_positions[i].y;
    float speed_factor = (float)speed_multiplier / 100.0f;

    // Organic wavy calculations
    float wave1 = sinf(led_x * 0.5f + (float)time_ms * 0.0006f * speed_factor);
    float wave2 = cosf(led_y * 0.7f - (float)time_ms * 0.0004f * speed_factor);
    float val = (wave1 + wave2) / 2.0f;

    // Aurora Hue scale (shifts slowly between cyan, green, and deep violet/indigo)
    float hue = 140.0f + (val * 80.0f) + fmodf((float)time_ms * 0.005f, 40.0f);
    float sat = 0.95f;
    float max_v = (float)CONFIG_ZMK_RGB_PLUS_DEFAULT_BRIGHTNESS / 100.0f;
    float brightness = max_v * (0.4f + (val + 1.0f) * 0.3f);

    return hsv_to_rgb(hue, sat, brightness);
}

static struct led_rgb render_fire(int i, int64_t time_ms) {
    float led_x = led_positions[i].x;
    float speed_factor = (float)speed_multiplier / 100.0f;
    
    // Per-LED flicker noise
    uint32_t seed = (uint32_t)(i * 347 + time_ms / (int64_t)(100.0f / speed_factor));
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    float rand_f = (float)(seed % 100) / 100.0f;

    // Fire palette: Deep red through glowing golden yellow
    float hue = 5.0f + (rand_f * 25.0f) + (led_x * 2.0f);
    float sat = 1.0f;
    float max_v = (float)CONFIG_ZMK_RGB_PLUS_DEFAULT_BRIGHTNESS / 100.0f;
    float brightness = max_v * (0.25f + rand_f * 0.50f);

    return hsv_to_rgb(hue, sat, brightness);
}

static struct led_rgb render_star(int i, int64_t time_ms) {
    // Large prime numbers create stable pseudorandom offsets
    uint32_t seed = i * 4923 + 7;
    float period = 1000.0f + (float)(seed % 2000);
    float phase = (float)(seed % 5000);
    float speed_factor = (float)speed_multiplier / 100.0f;

    float t = ((float)time_ms * speed_factor + phase) / period;
    float pulse = sinf(t * M_PI);
    
    float brightness = 0.0f;
    float sat = 0.90f;
    // Only glow when the pulse is high to simulate stars twinkling independently
    if (pulse > 0.65f) {
        float max_v = (float)CONFIG_ZMK_RGB_PLUS_DEFAULT_BRIGHTNESS / 100.0f;
        brightness = max_v * (pulse - 0.65f) / 0.35f;
    }

    // Stars shift between warm gold (hue 40) and cool white/blue (hue 210)
    float hue = (seed % 2 == 0) ? 42.0f : 205.0f;
    if (hue == 205.0f) sat = 0.4f; // cool starry white

    return hsv_to_rgb(hue, sat, brightness);
}

static struct led_rgb render_rainbow(int i, int64_t time_ms) {
    float led_x = led_positions[i].x;
    float led_y = led_positions[i].y;
    float speed_factor = (float)speed_multiplier / 100.0f;

    // Directional 45 degree angle
    float angle = 45.0f * (M_PI / 180.0f);
    float coordinate_projection = led_x * cosf(angle) + led_y * sinf(angle);

    // Hue traverses space and time smoothly
    float hue = fmodf((coordinate_projection * 35.0f) + (float)time_ms * 0.04f * speed_factor, 360.0f);
    if (hue < 0.0f) hue += 360.0f;
    
    float max_v = (float)CONFIG_ZMK_RGB_PLUS_DEFAULT_BRIGHTNESS / 100.0f;
    return hsv_to_rgb(hue, 0.95f, max_v);
}

static void apply_reactive_overlays(int i, struct led_rgb *rgb, int64_t time_ms) {
    float led_x = led_positions[i].x;
    float led_y = led_positions[i].y;

    float max_lifetime = (float)CONFIG_ZMK_RGB_PLUS_RIPPLE_LIFETIME; 
    float velocity = ((float)CONFIG_ZMK_RGB_PLUS_RIPPLE_SPEED / 100.0f) / 1000.0f; // grid units per ms
    float ripple_width = 0.5f;

    float ripple_brightness_accumulation = 0.0f;

    for (int r = 0; r < MAX_RIPPLES; r++) {
        if (!ripples[r].active) continue;

        float dt = (float)(time_ms - ripples[r].start_time);
        if (dt > max_lifetime) {
            ripples[r].active = false;
            continue;
        }

        float distance = dist_2d(led_x, led_y, ripples[r].x, ripples[r].y);
        float current_wave_front = velocity * dt;

        float diff = distance - current_wave_front;
        // Gaussian bell curve profile around wave front
        float intensity = expf(-(diff * diff) / (2.0f * ripple_width * ripple_width));
        float decay = 1.0f - (dt / max_lifetime);

        ripple_brightness_accumulation += intensity * decay;
    }

    if (ripple_brightness_accumulation > 0.0f) {
        if (ripple_brightness_accumulation > 1.0f) {
            ripple_brightness_accumulation = 1.0f;
        }

        // Blend: Ripple flashes white/light cyan over the background color
        float max_v = (float)CONFIG_ZMK_RGB_PLUS_DEFAULT_BRIGHTNESS / 100.0f;
        float base_intensity = 1.0f - ripple_brightness_accumulation;

        rgb->r = (uint8_t)((float)rgb->r * base_intensity + 255.0f * ripple_brightness_accumulation * max_v);
        rgb->g = (uint8_t)((float)rgb->g * base_intensity + 255.0f * ripple_brightness_accumulation * max_v);
        rgb->b = (uint8_t)((float)rgb->b * base_intensity + 255.0f * ripple_brightness_accumulation * max_v);
    }
}

/* Main render work handler */
static void render_frame(void) {
    const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
    if (!device_is_ready(strip)) return;

    init_led_coordinates();
    int64_t now = k_uptime_get();

    /* 1. Heat decay calculation */
    float cooling_rate = (float)CONFIG_ZMK_RGB_PLUS_HEATMAP_COOLING / 100.0f;
    static int64_t last_decay_time = 0;
    float dt = (last_decay_time == 0) ? 0.02f : (float)(now - last_decay_time) / 1000.0f;
    last_decay_time = now;

    for (int k = 0; k < KEY_COUNT; k++) {
        heatmap[k].heat -= cooling_rate * dt;
        if (heatmap[k].heat < 0.0f) heatmap[k].heat = 0.0f;
    }

    /* 2. Process each LED */
    for (int i = 0; i < LED_COUNT; i++) {
        struct led_rgb base_color = {0, 0, 0};

        switch (active_effect) {
            case RGB_PLUS_EFF_AURORA:
                base_color = render_aurora(i, now);
                break;
            case RGB_PLUS_EFF_FIRE:
                base_color = render_fire(i, now);
                break;
            case RGB_PLUS_EFF_STAR:
                base_color = render_star(i, now);
                break;
            case RGB_PLUS_EFF_RAINBOW:
                base_color = render_rainbow(i, now);
                break;
            case RGB_PLUS_EFF_HEATMAP: {
                // Find nearby keys to average spatial heatmap
                float accum_heat = 0.0f;
                float total_weight = 0.0f;
                
                for (int k = 0; k < KEY_COUNT; k++) {
                    float dist = dist_2d(led_positions[i].x, led_positions[i].y, key_positions[k].x, key_positions[k].y);
                    float weight = 1.0f / (dist + 0.1f);
                    accum_heat += heatmap[k].heat * weight;
                    total_weight += weight;
                }
                
                float led_heat = accum_heat / total_weight;
                if (led_heat > 1.0f) led_heat = 1.0f;

                // Color ramp: Cold (blue/purple) -> Medium (cyan/green) -> Hot (white-hot red)
                float hue = 250.0f - (led_heat * 250.0f); // scales 250 (blue) down to 0 (red)
                float sat = 1.0f - (led_heat * 0.4f); // turns lighter white at extreme hot
                float max_v = (float)CONFIG_ZMK_RGB_PLUS_DEFAULT_BRIGHTNESS / 100.0f;
                float val = max_v * (0.15f + led_heat * 0.85f);

                base_color = hsv_to_rgb(hue, sat, val);
                break;
            }
            case RGB_PLUS_EFF_RIPPLE:
            default: {
                // Default black background when Ripple is selected so splash stands out
                base_color.r = 0;
                base_color.g = 0;
                base_color.b = 0;
                break;
            }
        }

        // Apply interactive splashes over the ambient effects if enabled
        if (reactive_overlays_enabled && active_effect != RGB_PLUS_EFF_RIPPLE && active_effect != RGB_PLUS_EFF_HEATMAP) {
            apply_reactive_overlays(i, &base_color, now);
        } else if (active_effect == RGB_PLUS_EFF_RIPPLE) {
            apply_reactive_overlays(i, &base_color, now);
        }

        led_buffer[i] = base_color;
    }

    // Scale final frame brightness
    if (brightness_multiplier < 100) {
        for (int i = 0; i < LED_COUNT; i++) {
            led_buffer[i].r = (uint8_t)(((uint32_t)led_buffer[i].r * brightness_multiplier) / 100);
            led_buffer[i].g = (uint8_t)(((uint32_t)led_buffer[i].g * brightness_multiplier) / 100);
            led_buffer[i].b = (uint8_t)(((uint32_t)led_buffer[i].b * brightness_multiplier) / 100);
        }
    }

    led_strip_update_rgb(strip, led_buffer, LED_COUNT);
}

/* Frame scheduler callback */
static void rgb_work_handler(struct k_work *work) {
    bool is_usb = false;

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    is_usb = zmk_usb_is_powered();
#endif

    int fps = CONFIG_ZMK_RGB_PLUS_FPS_USB;

    if (IS_ENABLED(CONFIG_ZMK_RGB_PLUS_BATTERY_SAVER) && !is_usb) {
        fps = CONFIG_ZMK_RGB_PLUS_FPS_BATTERY;
    }

    if (fps <= 0) {
        // Disables thread execution completely
        k_work_reschedule(&rgb_work, K_MSEC(250)); // sleep tick
        return;
    }

    render_frame();
    k_work_reschedule(&rgb_work, K_MSEC(1000 / fps));
}

/* Initialization */
static int zmk_rgb_plus_init(void) {
#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ext_power_generic)
    if (gpio_is_ready_dt(&ext_power_gpio)) {
        gpio_pin_configure_dt(&ext_power_gpio, GPIO_OUTPUT_ACTIVE);
    }
#endif
    init_led_coordinates();
    k_work_init_delayable(&rgb_work, rgb_work_handler);
    k_work_reschedule(&rgb_work, K_MSEC(100));
    return 0;
}

SYS_INIT(zmk_rgb_plus_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* custom ZMK behavior implementation */

static int on_rgb_plus_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    uint32_t cmd = binding->param1;

    switch (cmd) {
        case RGB_PLUS_CMD_EFF_NEXT:
            active_effect = (active_effect + 1) % RGB_PLUS_EFF_COUNT;
            break;
        case RGB_PLUS_CMD_EFF_PREV:
            if (active_effect == 0) {
                active_effect = RGB_PLUS_EFF_COUNT - 1;
            } else {
                active_effect--;
            }
            break;
        case RGB_PLUS_CMD_REAC_TOG:
            reactive_overlays_enabled = !reactive_overlays_enabled;
            break;
        case RGB_PLUS_CMD_SPD_INC:
            speed_multiplier += 20;
            if (speed_multiplier > 300) speed_multiplier = 300;
            break;
        case RGB_PLUS_CMD_SPD_DEC:
            speed_multiplier -= 20;
            if (speed_multiplier < 20) speed_multiplier = 20;
            break;
        case RGB_PLUS_CMD_BRI_INC:
            brightness_multiplier += 10;
            if (brightness_multiplier > 100) brightness_multiplier = 100;
            break;
        case RGB_PLUS_CMD_BRI_DEC:
            brightness_multiplier -= 10;
            if (brightness_multiplier < 0) brightness_multiplier = 0;
            break;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_rgb_plus_binding_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api rgb_plus_behavior_api = {
    .binding_pressed = on_rgb_plus_binding_pressed,
    .binding_released = on_rgb_plus_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0,
                        NULL,
                        NULL,
                        NULL,
                        NULL,
                        POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &rgb_plus_behavior_api);
