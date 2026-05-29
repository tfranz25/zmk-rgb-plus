# 🌟 ZMK RGB Plus

A Standalone, Layout-Agnostic Advanced RGB Lighting Module for ZMK Keyboards.

`zmk-rgb-plus` is an out-of-tree ZMK module that brings highly interactive, reactive, and organic lighting animations to your keyboard. Unlike traditional ZMK underglow animations which are simple global or linear cycles, `zmk-rgb-plus` automatically queries your physical keyboard geometry at compile-time, allowing you to run ripples, typing heatmaps, and spatial rainbows perfectly aligned to your keyboard's key positions!

---

## 🤖 AI Disclosure

This ZMK module was built with the assistance of AI tools. The code has been reviewed, tweaked, and tested on [TibbyPad](https://github.com/tfranz25/tibbypad-module), but as with all AI-assisted firmware, please review the configurations before flashing it to your device.

---

## ✨ Features

*   🌊 **Reactive Typing Splash/Ripples**: Waves expand radially outward from key press coordinates. Multiple key presses spawn compounding waves that blend together seamlessly.
*   🔥 **Fireplace Flickering**: A cozy campfire animation. LEDs randomly flicker between hot golden yellow, glowing orange, and deep charcoal red.
*   🌌 **Aurora Borealis Flow**: A slow, wavy, organic blend of shifting greens, cyans, and deep violet/indigo.
*   ✨ **Starry Twinkle**: Independent stars in neutral white or cool white/blue that twinkle and fade across the board.
*   🌡️ **Typing Heatmap**: Tracks key hit frequency and translates "hot" spots into temperature color ramps across adjacent LEDs.
*   🌈 **Directional Rainbow Wave**: A sweeping hue cycle that travels along a customizable 2D angle (e.g. 45 degrees) across the board.
*   🔋 **Smart Power Saving**: An integrated battery saver that monitors USB power status and automatically drops the animation frame rate (e.g. to 0 FPS / sleep) when running wireless to preserve battery life. **Integrates natively with the [zmk-usb-rgb-idle-bypass](https://github.com/tfranz25/zmk-usb-rgb-idle-bypass) module to keep lighting active indefinitely when connected via USB, while cleanly powering off LEDs and fully suspending animation threads when idle on battery.**

---

## ⚙️ How It Works

1.  **Compile-Time Coordinate Parsing**: Using ZMK's layout system (`DT_CHOSEN(zmk_physical_layout)`), the module reads the raw `(X, Y)` coordinate of every key at build time.
2.  **Underglow Spatial Assignment**:
    *   **Per-Key Backlight**: If your LED strip length matches your key count, LEDs map 1-to-1 to your key coordinates.
    *   **Underglow Strips**: If your LED count differs (e.g. a macro pad with bottom-facing edge strips), the module automatically calculates your layout's physical boundary and clockwise distributes the LEDs uniformly around the outer perimeter!
    *   **Manual Coordinates**: You can explicitly override LED positions in your Devicetree overlay for advanced setups.

---

## 🚀 Installation

To incorporate this module into your ZMK config, complete these three simple steps:

### 1. Update `config/west.yml`
Add the module to your ZMK manifest as a project:

```yaml
manifest:
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    # Add ZMK RGB Plus:
    - name: zmk-rgb-plus
      url: https://github.com/tfranz25/zmk-rgb-plus
      revision: main
```

---

### 2. Enable in Kconfig (`<keyboard>.conf`)
Enable the custom module and configure its frame rates, battery saving, and ripple dynamics:

```ini
# Core ZMK underglow configuration
CONFIG_ZMK_EXT_POWER=y
CONFIG_ZMK_RGB_UNDERGLOW=y

# Enable ZMK RGB Plus
CONFIG_ZMK_RGB_PLUS=y

# Power Management
CONFIG_ZMK_RGB_PLUS_BATTERY_SAVER=y
CONFIG_ZMK_RGB_PLUS_FPS_USB=40
CONFIG_ZMK_RGB_PLUS_FPS_BATTERY=0  # Disables animations on battery (set to 5 for slow-mode)

# Optional customization
CONFIG_ZMK_RGB_PLUS_HEATMAP_COOLING=90    # Cool down rate (percent per second)
CONFIG_ZMK_RGB_PLUS_RAINBOW_ANGLE=45      # Traveling angle of the rainbow wave (degrees)
CONFIG_ZMK_RGB_PLUS_RIPPLE_MAX_RADIUS=300 # Maximum travel distance for ripples (300 = 3 keys)
```

---

### 3. Bind Control Keys in your Keymap (`<keyboard>.keymap`)
First, include the module's keymap bindings header at the top of your `.keymap` file. If your board has a physical power gate/regulator (like the Xiao BLE), you will also want to include ZMK's external power header:

```devicetree
#include <dt-bindings/zmk/rgb_plus.h>
#include <dt-bindings/zmk/ext_power.h>
```

Then, define the custom behavior and bind the actions to your key layers to control your lighting on the fly!

#### A. Declare the Behavior in Devicetree
At the top level of your `.keymap` file (outside the `/` root or within your `behaviors` block), declare:

```devicetree
/ {
    behaviors {
        rgb_plus: rgb_plus {
            compatible = "zmk,behavior-rgb-plus";
            label = "RGB_PLUS";
            #binding-cells = <1>;
        };
    };
};
```

#### B. Bind the Keys in your Layout
Map the control actions using these parameter definitions:

| Binding | Action | Description |
| :--- | :--- | :--- |
| `&rgb_plus eff_next` | `EFF_NEXT` | Cycles to the next animation. |
| `&rgb_plus eff_prev` | `EFF_PREV` | Cycles to the previous animation. |
| `&rgb_plus reac_tog` | `REAC_TOG` | Toggles reactive overlay (e.g. typing ripples over ambient lighting). |
| `&rgb_plus spd_inc` | `SPD_INC` | Increases animation speed / wave propagation. |
| `&rgb_plus spd_dec` | `SPD_DEC` | Decreases animation speed. |
| `&rgb_plus bri_inc` | `BRI_INC` | Increases master brightness (steps of 10%). |
| `&rgb_plus bri_dec` | `BRI_DEC` | Decreases master brightness (steps of 10%). |

```devicetree
// Example binding in a macro pad layer:
bindings = <
    &rgb_plus eff_next   &rgb_plus eff_prev   &rgb_plus reac_tog   &rgb_plus spd_inc
>;
```

#### C. External Power Control (Recommended)
If your keyboard hardware includes a VCC power gate or regulator to cut power to the LED strip completely when sleeping/idle (e.g. Xiao BLE, TibbyPad), you must enable ZMK's external power library (`CONFIG_ZMK_EXT_POWER=y`) in your `.conf` file and bind these control keys:

| Binding | Action | Description |
| :--- | :--- | :--- |
| `&ext_power EP_ON` | `EP_ON` | Turns on the physical hardware VCC gate to power the LED strip. |
| `&ext_power EP_OFF` | `EP_OFF` | Turns off the physical hardware VCC gate to save max power. |
| `&ext_power EP_TOG` | `EP_TOG` | Toggles the physical hardware VCC gate power. |

#### C. Integration with `zmk-usb-rgb-idle-bypass`
If you want to keep your advanced animations active indefinitely while connected via USB, but still want them to turn off automatically during idle states when running on battery (Bluetooth), you can pull in the [zmk-usb-rgb-idle-bypass](https://github.com/tfranz25/zmk-usb-rgb-idle-bypass) module. 

`zmk-rgb-plus` exports custom state control APIs (`zmk_rgb_plus_on()`, `zmk_rgb_plus_off()`) that are automatically detected and utilized by the idle bypass module to suspend the rendering engine and save battery power seamlessly!

---

## 🛠️ Advanced Customization

### Explicit LED Coordinates
By default, the module distributes LEDs evenly around the outer perimeter of your key layout. If you want absolute precision, you can specify exactly where your LEDs are positioned in your `<keyboard>.overlay` file using the `zmk,rgb-plus-layout` compatible compatible node:

```devicetree
/ {
    rgb_plus_config {
        compatible = "zmk,rgb-plus-layout";
        
        // Define X, Y coordinates in grid units (e.g. 100 = 1U key width) for each LED
        led-coordinates = 
            <0   0>, <100   0>, <200   0>, <300   0>, // Top Row underglow spots
            <0 100>, <100 100>, <200 100>, <300 100>  // Next Row underglow spots
            /* ... map all LEDs in your chain length ... */
            ;
    };
};
```

---

## 📊 Kconfig Options Reference

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `CONFIG_ZMK_RGB_PLUS` | boolean | `n` | Master switch to enable advanced effects. |
| `CONFIG_ZMK_RGB_PLUS_BATTERY_SAVER` | boolean | `y` | Enables framerate down-scaling on battery. |
| `CONFIG_ZMK_RGB_PLUS_AUTO_OFF_IDLE` | boolean | `y` | Automatically turns off lights and suspends rendering when the keyboard goes idle. |
| `CONFIG_ZMK_RGB_PLUS_FPS_USB` | integer | `40` | Framerate (FPS) when powered via USB (10-100). |
| `CONFIG_ZMK_RGB_PLUS_FPS_BATTERY` | integer | `0` | Framerate (FPS) on battery. Set to `0` to deep-sleep. |
| `CONFIG_ZMK_RGB_PLUS_RIPPLE_SPEED` | integer | `500` | Expansion velocity of key waves (hundredths of grid units/sec). |
| `CONFIG_ZMK_RGB_PLUS_RIPPLE_LIFETIME` | integer | `800` | Ripple decay duration (in milliseconds). |
| `CONFIG_ZMK_RGB_PLUS_RIPPLE_MAX_RADIUS` | integer | `300` | Ripple maximum travel distance (hundredths of grid units, e.g. 300 = 3 keys). |
| `CONFIG_ZMK_RGB_PLUS_HEATMAP_COOLING` | integer | `90` | Thermal cooling rate percent per second (1-100). |
| `CONFIG_ZMK_RGB_PLUS_RAINBOW_ANGLE` | integer | `45` | Directional rainbow wave angle in degrees (0-360). |
| `CONFIG_ZMK_RGB_PLUS_DEFAULT_BRIGHTNESS` | integer | `80` | Default brightness scaling percentage (5-100). |
