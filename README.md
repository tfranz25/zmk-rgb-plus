# ZMK RGB Plus: Reusable, Layout-Agnostic Advanced RGB Lighting

`zmk-rgb-plus` is a standalone, high-performance, and layout-independent RGB animation module for ZMK keyboards. 

Rather than hardcoding layout grids, `zmk-rgb-plus` automatically parses your keyboard's key coordinates from your Devicetree at compile-time and uses spatial interpolation to map reactive splashes, directional rainbow sweeps, and fading trails directly to your board's geometry!

---

## Features
1. **Typing Splash/Ripples**: Waves expand radially outward from key press coordinates.
2. **Aurora Flow**: Shifting diagonal waves of green, violet, and deep indigo.
3. **Fireplace Flickering**: Warm embers with organic micro-flickers.
4. **Starry Twinkle**: Randomly sparking gold and cyan stars fading across the board.
5. **Typing Heatmap**: Tracks keys by hit frequency and spreads a temperature gradient.
6. **Smart Battery Saver**: Checks USB status and drops to 0 FPS (deep sleep) on battery.

---

## Step 1: Push this Module to GitHub
Since you build your firmware via GitHub Actions, the compiler needs to fetch this module from GitHub. Follow these quick terminal steps to initialize and push:

```bash
# Navigate to the module directory
cd /home/tobias/dev/zmk-rgb-plus

# Initialize git repository
git init -b main

# Add and commit the files
git add .
git commit -m "Initialize zmk-rgb-plus module"

# Create a new repository on your GitHub (named 'zmk-rgb-plus')
# Then add the remote and push (replace tfranz25 with your actual GitHub username):
git remote add origin https://github.com/tfranz25/zmk-rgb-plus.git
git push -u origin main
```

---

## Step 2: Add the Module to your Keyboard's `west.yml`
Open your `tibbypad-module`'s `config/west.yml` and add `zmk-rgb-plus` to the `projects` list (pointing to your new GitHub repository):

```yaml
manifest:
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    # Add your brand-new module here:
    - name: zmk-rgb-plus
      url: https://github.com/tfranz25/zmk-rgb-plus
      revision: main
```

---

## Step 3: Enable the Module in Kconfig
Add the following switches to your keyboard's `.conf` file (e.g. `tibbypad.conf`):

```ini
# Core ZMK RGB (required)
CONFIG_ZMK_RGB_UNDERGLOW=y

# Enable ZMK RGB Plus
CONFIG_ZMK_RGB_PLUS=y
CONFIG_ZMK_RGB_PLUS_BATTERY_SAVER=y
CONFIG_ZMK_RGB_PLUS_FPS_USB=40
CONFIG_ZMK_RGB_PLUS_FPS_BATTERY=0  # Disables animations entirely on battery to save power
```

---

## Step 4: Map Controls in your Keymap (`tibbypad.keymap`)
To let you control your advanced animations on the fly, define the custom behavior in your keymap overlay:

### 1. Declare the Behavior in Devicetree
At the top level of your `tibbypad.keymap` (outside the `/` root or within the root), define the behavior node:

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

### 2. Bind the Keys
Add these keycodes into your layout layers to cycle effects and adjust animation speed:

```devicetree
// Example bindings:
bindings = <
    &rgb_plus 0   // Next Effect (Aurora -> Fire -> Starry -> Rainbow -> Ripple -> Heatmap)
    &rgb_plus 1   // Previous Effect
    &rgb_plus 2   // Toggle reactive overlay
    &rgb_plus 3   // Speed Up animations
    &rgb_plus 4   // Slow Down animations
>;
```
