# PWM Rainbow Extension Notes

This fork adds a PWM backend and a rainbow toggle behavior to `caksoylar/zmk-rgbled-widget` while keeping the original GPIO backend and existing indicator behaviors.

The implementation in this branch is based on the upstream `v0.3` code path, for use with ZMK `v0.3`.

## Summary

- Existing `rgbled_adapter` / GPIO behavior is preserved.
- New `rgbled_pwm_adapter` shield targets Seeeduino XIAO BLE / nRF52840 onboard RGB LED.
- New `CONFIG_RGBLED_WIDGET_BACKEND_GPIO` and `CONFIG_RGBLED_WIDGET_BACKEND_PWM` choice selects the LED backend.
- New `&ind_rainbow` behavior toggles continuous rainbow mode.
- New `CONFIG_RGBLED_WIDGET_RAINBOW_DEFAULT_ON` starts rainbow mode at boot.
- Existing battery, connectivity, and layer indicators take priority over rainbow.

## Hardware Target

The PWM adapter is intended for the Seeeduino XIAO BLE onboard active-low RGB LED.

| Color | nRF52840 pin | PWM output |
| --- | --- | --- |
| Red | P0.26 | PWM_OUT0 |
| Green | P0.30 | PWM_OUT1 |
| Blue | P0.06 | PWM_OUT2 |

The adapter uses `PWM_POLARITY_INVERTED`, so C code treats `0` as off and `255` as full brightness.

## Added Files

```text
boards/shields/rgbled_pwm_adapter/
  Kconfig.shield
  Kconfig.defconfig
  rgbled_pwm_adapter.overlay

dts/bindings/behaviors/zmk,behavior-rgbled-rainbow.yaml
src/behaviors/behavior_rgbled_rainbow.c
```

The existing files below were extended:

```text
CMakeLists.txt
Kconfig
dts/behaviors/rgbled_widget.dtsi
include/zmk_rgbled_widget/widget.h
src/widget.c
```

## Kconfig

The backend is selected with:

```conf
CONFIG_RGBLED_WIDGET_BACKEND_GPIO=y
# or
CONFIG_RGBLED_WIDGET_BACKEND_PWM=y
```

For the PWM rainbow mode:

```conf
CONFIG_RGBLED_WIDGET=y
CONFIG_RGBLED_WIDGET_BACKEND_PWM=y
CONFIG_RGBLED_WIDGET_RAINBOW=y
CONFIG_RGBLED_WIDGET_RAINBOW_DEFAULT_ON=y
```

Rainbow tuning defaults:

```conf
CONFIG_RGBLED_WIDGET_RAINBOW_INTERVAL_MS=80
CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS=168
```

`CONFIG_RGBLED_WIDGET_RAINBOW_DURATION_MS` remains available from the initial implementation, but the current `&ind_rainbow` behavior is a continuous ON/OFF toggle and does not use a fixed duration.

## Build Configuration

Use the PWM adapter instead of the original GPIO adapter:

```yaml
include:
  - board: seeeduino_xiao_ble
    shield: your_keyboard rgbled_pwm_adapter
```

When using this fork from a ZMK config, point `west.yml` to the v0.3-compatible branch:

```yaml
manifest:
  remotes:
    - name: 84ix
      url-base: https://github.com/84ix
  projects:
    - name: zmk-rgbled-widget
      remote: 84ix
      revision: feature/pwm-rainbow-v0.3
```

## Keymap Behavior

Include the behavior definitions:

```dts
#include <behaviors/rgbled_widget.dtsi>
```

Map the rainbow toggle:

```dts
&ind_rainbow
```

Pressing `&ind_rainbow` toggles rainbow mode. If rainbow was started by `CONFIG_RGBLED_WIDGET_RAINBOW_DEFAULT_ON`, pressing the behavior turns it off; pressing it again turns it on.

## Indicator Priority

Rainbow is intentionally lower priority than the original widget functions.

When battery, connectivity, or layer indicators write to the LED, those colors are shown first. Rainbow remains enabled, but its work item waits until the original indicator has finished, then resumes the animation.

This preserves the original purpose of the widget:

1. Battery, connectivity, and layer indications.
2. Rainbow animation when no other indicator is active.

## Implementation Notes

`src/widget.c` now has a small backend abstraction:

- GPIO backend maps RGB values to on/off LED API calls.
- PWM backend maps RGB values from `0..255` to `pwm_set_dt()` duty cycles.

Rainbow uses integer HSV-to-RGB conversion and `k_work_delayable`.

The current hue step is `4` per update. The effective transition speed is controlled mostly by `CONFIG_RGBLED_WIDGET_RAINBOW_INTERVAL_MS`, which defaults to `80`.

## Known Scope

- The PWM adapter is written for Seeeduino XIAO BLE / nRF52840.
- The original GPIO adapter is still available for existing users.
- Automatic rainbow on every keypress is not implemented.
- The branch is intended for ZMK `v0.3`; newer ZMK versions may need the newer endpoint API.
