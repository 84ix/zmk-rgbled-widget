#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_GPIO)
#include <zephyr/drivers/led.h>
#endif
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_PWM)
#include <zephyr/drivers/pwm.h>
#endif
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/peripheral.h>

#if __has_include(<zmk/split/central.h>)
#include <zmk/split/central.h>
#else
#include <zmk/split/bluetooth/central.h>
#endif

#include <zephyr/logging/log.h>

#include <zmk_rgbled_widget/widget.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_GPIO)
#define LED_GPIO_NODE_ID DT_COMPAT_GET_ANY_STATUS_OKAY(gpio_leds)

BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_red)),
             "An alias for a red LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_green)),
             "An alias for a green LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_blue)),
             "An alias for a blue LED is not found for RGBLED_WIDGET");
#endif

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_PWM)
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_red_pwm)),
             "An alias for a red PWM LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_green_pwm)),
             "An alias for a green PWM LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_blue_pwm)),
             "An alias for a blue PWM LED is not found for RGBLED_WIDGET");
#endif

BUILD_ASSERT(!(SHOW_LAYER_CHANGE && SHOW_LAYER_COLORS),
             "CONFIG_RGBLED_WIDGET_SHOW_LAYER_CHANGE and CONFIG_RGBLED_WIDGET_SHOW_LAYER_COLORS "
             "are mutually exclusive");

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_GPIO)
// GPIO-based LED device and indices of red/green/blue LEDs inside its DT node
static const struct device *led_dev = DEVICE_DT_GET(LED_GPIO_NODE_ID);
static const uint8_t rgb_idx[] = {DT_NODE_CHILD_IDX(DT_ALIAS(led_red)),
                                  DT_NODE_CHILD_IDX(DT_ALIAS(led_green)),
                                  DT_NODE_CHILD_IDX(DT_ALIAS(led_blue))};
#endif

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_PWM)
static const struct pwm_dt_spec red_pwm = PWM_DT_SPEC_GET(DT_ALIAS(led_red_pwm));
static const struct pwm_dt_spec green_pwm = PWM_DT_SPEC_GET(DT_ALIAS(led_green_pwm));
static const struct pwm_dt_spec blue_pwm = PWM_DT_SPEC_GET(DT_ALIAS(led_blue_pwm));
#endif

static bool rgbled_backend_is_ready(void) {
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_GPIO)
    if (!device_is_ready(led_dev)) {
        LOG_ERR("RGB LED GPIO device not ready");
        return false;
    }
#elif IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_PWM)
    if (!pwm_is_ready_dt(&red_pwm) || !pwm_is_ready_dt(&green_pwm) ||
        !pwm_is_ready_dt(&blue_pwm)) {
        LOG_ERR("RGB LED PWM device not ready");
        return false;
    }
#endif

    return true;
}

// map from color values to names, for logging
static const char *color_names[] = {"black", "red",     "green", "yellow",
                                    "blue",  "magenta", "cyan",  "white"};

#if SHOW_LAYER_COLORS
static const uint8_t layer_color_idx[] = {
    CONFIG_RGBLED_WIDGET_LAYER_0_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_1_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_2_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_3_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_4_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_5_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_6_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_7_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_8_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_9_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_10_COLOR, CONFIG_RGBLED_WIDGET_LAYER_11_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_12_COLOR, CONFIG_RGBLED_WIDGET_LAYER_13_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_14_COLOR, CONFIG_RGBLED_WIDGET_LAYER_15_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_16_COLOR, CONFIG_RGBLED_WIDGET_LAYER_17_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_18_COLOR, CONFIG_RGBLED_WIDGET_LAYER_19_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_20_COLOR, CONFIG_RGBLED_WIDGET_LAYER_21_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_22_COLOR, CONFIG_RGBLED_WIDGET_LAYER_23_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_24_COLOR, CONFIG_RGBLED_WIDGET_LAYER_25_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_26_COLOR, CONFIG_RGBLED_WIDGET_LAYER_27_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_28_COLOR, CONFIG_RGBLED_WIDGET_LAYER_29_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_30_COLOR, CONFIG_RGBLED_WIDGET_LAYER_31_COLOR,
};
#endif

// log shorthands
#define LOG_CONN_CENTRAL(index, status, color_label)                                               \
    LOG_INF("Profile %d %s, blinking %s", index, status,                                           \
            color_names[CONFIG_RGBLED_WIDGET_CONN_COLOR_##color_label])
#define LOG_CONN_PERIPHERAL(status, color_label)                                                   \
    LOG_INF("Peripheral %s, blinking %s", status,                                                  \
            color_names[CONFIG_RGBLED_WIDGET_CONN_COLOR_##color_label])
#define LOG_BATTERY(battery_level, color_label)                                                    \
    LOG_INF("Battery level %d, blinking %s", battery_level,                                        \
            color_names[CONFIG_RGBLED_WIDGET_BATTERY_COLOR_##color_label])

// a blink work item as specified by the color and duration
struct blink_item {
    uint8_t color;
    uint16_t duration_ms;
    uint16_t sleep_ms;
};

// flag to indicate whether the initial boot up sequence is complete
static bool initialized = false;

// track current color for persistent indicators (layer color)
uint8_t led_current_color = 0;

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_KEY_IDLE_OFF)
static struct k_work_delayable key_idle_work;
static bool key_idle = false;
#endif

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_RAINBOW)
static struct k_work_delayable rainbow_work;
static bool rainbow_active = false;
static bool rainbow_enabled = false;
static int64_t rainbow_suppressed_until;
static int64_t rainbow_started_at;
static uint8_t rainbow_hue;
#endif

static void color_to_rgb(uint8_t color, uint8_t *red, uint8_t *green, uint8_t *blue) {
    *red = (color & BIT(0)) ? 255 : 0;
    *green = (color & BIT(1)) ? 255 : 0;
    *blue = (color & BIT(2)) ? 255 : 0;
}

static void set_rgb_leds_u8(uint8_t red, uint8_t green, uint8_t blue) {
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_GPIO)
    const uint8_t values[] = {red, green, blue};

    for (uint8_t pos = 0; pos < 3; pos++) {
        if (values[pos] > 0) {
            led_on(led_dev, rgb_idx[pos]);
        } else {
            led_off(led_dev, rgb_idx[pos]);
        }
    }
#elif IS_ENABLED(CONFIG_RGBLED_WIDGET_BACKEND_PWM)
    const uint32_t red_pulse = ((uint64_t)red_pwm.period * red) / 255;
    const uint32_t green_pulse = ((uint64_t)green_pwm.period * green) / 255;
    const uint32_t blue_pulse = ((uint64_t)blue_pwm.period * blue) / 255;

    pwm_set_dt(&red_pwm, red_pwm.period, red_pulse);
    pwm_set_dt(&green_pwm, green_pwm.period, green_pulse);
    pwm_set_dt(&blue_pwm, blue_pwm.period, blue_pulse);
#endif
}

// low-level method to control the LED
static void set_rgb_leds(uint8_t color, uint32_t duration_ms) {
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_KEY_IDLE_OFF)
    if (key_idle) {
        set_rgb_leds_u8(0, 0, 0);
        if (duration_ms > 0) {
            k_sleep(K_MSEC(duration_ms));
        }
        led_current_color = 0;
        return;
    }
#endif

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_RAINBOW)
    if (rainbow_active) {
        int64_t suppress_until =
            k_uptime_get() + MAX(duration_ms, CONFIG_RGBLED_WIDGET_INTERVAL_MS);

        if (suppress_until > rainbow_suppressed_until) {
            rainbow_suppressed_until = suppress_until;
        }
    }
#endif

    uint8_t red;
    uint8_t green;
    uint8_t blue;

    color_to_rgb(color, &red, &green, &blue);
    set_rgb_leds_u8(red, green, blue);

    if (duration_ms > 0) {
        k_sleep(K_MSEC(duration_ms));
    }
    led_current_color = color;
}

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_RAINBOW)
static void hsv_to_rgb_u8(uint8_t hue, uint8_t *red, uint8_t *green, uint8_t *blue) {
    uint8_t region = hue / 43;
    uint8_t remainder = (hue - (region * 43)) * 6;
    uint8_t p = 0;
    uint8_t q = 255 - remainder;
    uint8_t t = remainder;

    switch (region) {
    case 0:
        *red = 255;
        *green = t;
        *blue = p;
        break;
    case 1:
        *red = q;
        *green = 255;
        *blue = p;
        break;
    case 2:
        *red = p;
        *green = 255;
        *blue = t;
        break;
    case 3:
        *red = p;
        *green = q;
        *blue = 255;
        break;
    case 4:
        *red = t;
        *green = p;
        *blue = 255;
        break;
    default:
        *red = 255;
        *green = p;
        *blue = q;
        break;
    }
}

static uint8_t rainbow_brightness(void) {
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_RAINBOW_BREATHING)
    const uint32_t period = CONFIG_RGBLED_WIDGET_RAINBOW_BREATHING_PERIOD_MS;
    const uint32_t half_period = period / 2;
    const uint32_t phase = (k_uptime_get() - rainbow_started_at) % period;
    const uint32_t ramp = phase < half_period ? phase : period - phase;
    const uint32_t x = ((uint64_t)ramp * UINT16_MAX) / half_period;
    const uint32_t smooth =
        ((uint64_t)x * x * (3 * UINT16_MAX - 2 * x)) / ((uint64_t)UINT16_MAX * UINT16_MAX);
    const uint8_t minimum = MIN(CONFIG_RGBLED_WIDGET_RAINBOW_BREATHING_MIN_BRIGHTNESS,
                                CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS);

    return minimum +
           ((CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS - minimum) * smooth) / UINT16_MAX;
#else
    return CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS;
#endif
}

static void scale_brightness(uint8_t *red, uint8_t *green, uint8_t *blue) {
    const uint8_t brightness = rainbow_brightness();

    *red = (*red * brightness) / 255;
    *green = (*green * brightness) / 255;
    *blue = (*blue * brightness) / 255;
}

static void rainbow_work_handler(struct k_work *work) {
    if (!rainbow_active) {
        return;
    }

    int64_t remaining_suppression = rainbow_suppressed_until - k_uptime_get();
    if (remaining_suppression > 0) {
        k_work_schedule(&rainbow_work, K_MSEC(remaining_suppression));
        return;
    }

    uint8_t red;
    uint8_t green;
    uint8_t blue;

    hsv_to_rgb_u8(rainbow_hue, &red, &green, &blue);
    scale_brightness(&red, &green, &blue);
    set_rgb_leds_u8(red, green, blue);

    rainbow_hue += CONFIG_RGBLED_WIDGET_RAINBOW_HUE_STEP;
    k_work_schedule(&rainbow_work, K_MSEC(CONFIG_RGBLED_WIDGET_RAINBOW_INTERVAL_MS));
}

static void set_rainbow_active(bool active) {
    if (active) {
        rainbow_hue = 0;
        rainbow_suppressed_until = 0;
        rainbow_started_at = k_uptime_get();
        rainbow_active = true;
        k_work_cancel_delayable(&rainbow_work);
        k_work_schedule(&rainbow_work, K_NO_WAIT);
    } else {
        rainbow_active = false;
        k_work_cancel_delayable(&rainbow_work);
        set_rgb_leds_u8(0, 0, 0);
        led_current_color = 0;
    }
}

static void set_rainbow_enabled(bool enabled) {
    rainbow_enabled = enabled;
    set_rainbow_active(enabled);
}

void toggle_rainbow(void) {
    set_rainbow_enabled(!rainbow_enabled);
}

static int rainbow_activity_listener_cb(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);

    if (ev == NULL) {
        return 0;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_SLEEP:
        set_rainbow_active(false);
        break;
    case ZMK_ACTIVITY_ACTIVE:
        if (rainbow_enabled
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_KEY_IDLE_OFF)
            && !key_idle
#endif
        ) {
            set_rainbow_active(true);
        }
        break;
    default:
        break;
    }

    return 0;
}

ZMK_LISTENER(rainbow_activity_listener, rainbow_activity_listener_cb);
ZMK_SUBSCRIPTION(rainbow_activity_listener, zmk_activity_state_changed);
#endif

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_KEY_IDLE_OFF)
static void key_idle_work_handler(struct k_work *work) {
    key_idle = true;
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_RAINBOW)
    set_rainbow_active(false);
#else
    set_rgb_leds_u8(0, 0, 0);
    led_current_color = 0;
#endif
    LOG_INF("Turned off LED after keyboard inactivity");
}

static void reset_key_idle_timeout(void) {
    bool was_idle = key_idle;
    key_idle = false;

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_RAINBOW)
    if (was_idle && rainbow_enabled) {
        set_rainbow_active(true);
    }
#endif

    k_work_reschedule(&key_idle_work, K_MSEC(CONFIG_RGBLED_WIDGET_KEY_IDLE_TIMEOUT_MS));
}

static int key_idle_listener_cb(const zmk_event_t *eh) {
    if (initialized && as_zmk_position_state_changed(eh) != NULL) {
        reset_key_idle_timeout();
    }
    return 0;
}

ZMK_LISTENER(key_idle_listener, key_idle_listener_cb);
ZMK_SUBSCRIPTION(key_idle_listener, zmk_position_state_changed);
#endif

// define message queue of blink work items, that will be processed by a
// separate thread
K_MSGQ_DEFINE(led_msgq, sizeof(struct blink_item), 16, 1);

static void indicate_connectivity_internal(void) {
    struct blink_item blink = {.duration_ms = CONFIG_RGBLED_WIDGET_CONN_BLINK_MS};

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    switch (zmk_endpoints_selected().transport) {
    case ZMK_TRANSPORT_USB:
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_CONN_SHOW_USB)
        LOG_INF("USB connected, blinking %s", color_names[CONFIG_RGBLED_WIDGET_CONN_COLOR_USB]);
        blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_USB;
        break;
#endif
    default: // ZMK_TRANSPORT_BLE
#if IS_ENABLED(CONFIG_ZMK_BLE)
        uint8_t profile_index = zmk_ble_active_profile_index();
        if (zmk_ble_active_profile_is_connected()) {
            LOG_CONN_CENTRAL(profile_index, "connected", CONNECTED);
            blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED;
        } else if (zmk_ble_active_profile_is_open()) {
            LOG_CONN_CENTRAL(profile_index, "open", ADVERTISING);
            blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_ADVERTISING;
        } else {
            LOG_CONN_CENTRAL(profile_index, "not connected", DISCONNECTED);
            blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED;
        }
#endif
        break;
    }
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    if (zmk_split_bt_peripheral_is_connected()) {
        LOG_CONN_PERIPHERAL("connected", CONNECTED);
        blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED;
    } else {
        LOG_CONN_PERIPHERAL("not connected", DISCONNECTED);
        blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED;
    }
#endif

    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
}

static int led_output_listener_cb(const zmk_event_t *eh) {
    if (initialized) {
        indicate_connectivity();
    }
    return 0;
}

// debouncing to ignore all but last connectivity event, to prevent repeat blinks
static struct k_work_delayable indicate_connectivity_work;
static void indicate_connectivity_cb(struct k_work *work) { indicate_connectivity_internal(); }
void indicate_connectivity() { k_work_reschedule(&indicate_connectivity_work, K_MSEC(16)); }

ZMK_LISTENER(led_output_listener, led_output_listener_cb);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
// run led_output_listener_cb on endpoint and BLE profile change (on central)
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_CONN_SHOW_USB)
ZMK_SUBSCRIPTION(led_output_listener, zmk_endpoint_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(led_output_listener, zmk_ble_active_profile_changed);
#endif // IS_ENABLED(CONFIG_ZMK_BLE)
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
// run led_output_listener_cb on peripheral status change event
ZMK_SUBSCRIPTION(led_output_listener, zmk_split_peripheral_status_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static inline uint8_t get_battery_color(uint8_t battery_level) {
    if (battery_level == 0) {
        LOG_INF("Battery level undetermined (zero), blinking %s",
                color_names[CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MISSING]);
        return CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MISSING;
    }
    if (battery_level >= CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_HIGH) {
        LOG_BATTERY(battery_level, HIGH);
        return CONFIG_RGBLED_WIDGET_BATTERY_COLOR_HIGH;
    }
    if (battery_level >= CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_LOW) {
        LOG_BATTERY(battery_level, MEDIUM);
        return CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MEDIUM;
    }
    LOG_BATTERY(battery_level, LOW);
    return CONFIG_RGBLED_WIDGET_BATTERY_COLOR_LOW;
}

void indicate_battery(void) {
    struct blink_item blink = {.duration_ms = CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS};
    int retry = 0;

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_SELF) ||                                          \
    IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS)
    uint8_t battery_level = zmk_battery_state_of_charge();
    while (battery_level == 0 && retry++ < 10) {
        k_sleep(K_MSEC(100));
        battery_level = zmk_battery_state_of_charge();
    };

    blink.color = get_battery_color(battery_level);
    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
#endif

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS) ||                                   \
    IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_ONLY_PERIPHERALS)
    for (uint8_t i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        uint8_t peripheral_level;
#if __has_include(<zmk/split/central.h>)
        int ret = zmk_split_central_get_peripheral_battery_level(i, &peripheral_level);
#else
        int ret = zmk_split_get_peripheral_battery_level(i, &peripheral_level);
#endif
        if (ret == 0) {
            retry = 0;
            while (peripheral_level == 0 && retry++ < (CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS +
                                                       CONFIG_RGBLED_WIDGET_INTERVAL_MS) /
                                                          100) {
                k_sleep(K_MSEC(100));
#if __has_include(<zmk/split/central.h>)
                zmk_split_central_get_peripheral_battery_level(i, &peripheral_level);
#else
                zmk_split_get_peripheral_battery_level(i, &peripheral_level);
#endif
            }

            LOG_INF("Got battery level for peripheral %d:", i);
            blink.color = get_battery_color(peripheral_level);
            k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
        } else {
            LOG_ERR("Error looking up battery level for peripheral %d", i);
        }
    }
#endif
}

static int led_battery_listener_cb(const zmk_event_t *eh) {
    if (!initialized) {
        return 0;
    }

    // check if we are in critical battery levels at state change, blink if we are
    uint8_t battery_level = as_zmk_battery_state_changed(eh)->state_of_charge;

    if (battery_level > 0 && battery_level <= CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_CRITICAL) {
        LOG_BATTERY(battery_level, CRITICAL);

        struct blink_item blink = {.duration_ms = CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS,
                                   .color = CONFIG_RGBLED_WIDGET_BATTERY_COLOR_CRITICAL};
        k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
    }
    return 0;
}

// run led_battery_listener_cb on battery state change event
ZMK_LISTENER(led_battery_listener, led_battery_listener_cb);
ZMK_SUBSCRIPTION(led_battery_listener, zmk_battery_state_changed);
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

uint8_t led_layer_color = 0;
#if SHOW_LAYER_COLORS
void update_layer_color(void) {
    uint8_t index = zmk_keymap_highest_layer_active();

    if (led_layer_color != layer_color_idx[index]) {
        led_layer_color = layer_color_idx[index];
        struct blink_item color = {.color = led_layer_color};
        LOG_INF("Setting layer color to %s for layer %d", color_names[led_layer_color], index);
        k_msgq_put(&led_msgq, &color, K_NO_WAIT);
    }
}

static int led_layer_color_listener_cb(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);

    // check if this is indeed an activity state changed event
    if (ev != NULL) {
        switch (ev->state) {
        case ZMK_ACTIVITY_SLEEP:
            LOG_INF("Detected sleep activity state, turn off LED");
            set_rgb_leds(0, 0);
            break;
        default: // not handling IDLE and ACTIVE yet
            break;
        }
        return 0;
    }

    // it must be a layer change event instead
    if (initialized) {
        update_layer_color();
    }
    return 0;
}

// run layer_color_listener_cb on layer status change event and activity state event
ZMK_LISTENER(led_layer_color_listener, led_layer_color_listener_cb);
ZMK_SUBSCRIPTION(led_layer_color_listener, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(led_layer_color_listener, zmk_activity_state_changed);
#endif // SHOW_LAYER_COLORS

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
void indicate_layer(void) {
    uint8_t index = zmk_keymap_highest_layer_active();
    static const struct blink_item blink = {.duration_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS,
                                            .color = CONFIG_RGBLED_WIDGET_LAYER_COLOR,
                                            .sleep_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS};
    static const struct blink_item last_blink = {.duration_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS,
                                                 .color = CONFIG_RGBLED_WIDGET_LAYER_COLOR};
    LOG_INF("Blinking %d times %s for layer change", index,
            color_names[CONFIG_RGBLED_WIDGET_LAYER_COLOR]);

    for (int i = 0; i < index; i++) {
        if (i < index - 1) {
            k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
        } else {
            k_msgq_put(&led_msgq, &last_blink, K_NO_WAIT);
        }
    }
}
#endif // !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#if SHOW_LAYER_CHANGE
static struct k_work_delayable layer_indicate_work;

static int led_layer_listener_cb(const zmk_event_t *eh) {
    // ignore if not initialized yet or layer off events
    if (initialized && as_zmk_layer_state_changed(eh)->state) {
        k_work_reschedule(&layer_indicate_work, K_MSEC(CONFIG_RGBLED_WIDGET_LAYER_DEBOUNCE_MS));
    }
    return 0;
}

static void indicate_layer_cb(struct k_work *work) { indicate_layer(); }

ZMK_LISTENER(led_layer_listener, led_layer_listener_cb);
ZMK_SUBSCRIPTION(led_layer_listener, zmk_layer_state_changed);
#endif // SHOW_LAYER_CHANGE

extern void led_process_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    k_work_init_delayable(&indicate_connectivity_work, indicate_connectivity_cb);

#if SHOW_LAYER_CHANGE
    k_work_init_delayable(&layer_indicate_work, indicate_layer_cb);
#endif
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_RAINBOW)
    k_work_init_delayable(&rainbow_work, rainbow_work_handler);
#endif
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_KEY_IDLE_OFF)
    k_work_init_delayable(&key_idle_work, key_idle_work_handler);
#endif

    while (true) {
        // wait until a blink item is received and process it
        struct blink_item blink;
        k_msgq_get(&led_msgq, &blink, K_FOREVER);
        if (blink.duration_ms > 0) {
            LOG_DBG("Got a blink item from msgq, color %d, duration %d", blink.color,
                    blink.duration_ms);

            // Blink the leds, using a separation blink if necessary
            if (blink.color == led_current_color && blink.color > 0) {
                set_rgb_leds(0, CONFIG_RGBLED_WIDGET_INTERVAL_MS);
            }
            set_rgb_leds(blink.color, blink.duration_ms);
            if (blink.color == led_layer_color && blink.color > 0) {
                set_rgb_leds(0, CONFIG_RGBLED_WIDGET_INTERVAL_MS);
            }
            // wait interval before processing another blink
            set_rgb_leds(led_layer_color,
                         blink.sleep_ms > 0 ? blink.sleep_ms : CONFIG_RGBLED_WIDGET_INTERVAL_MS);

        } else {
            LOG_DBG("Got a layer color item from msgq, color %d", blink.color);
            set_rgb_leds(blink.color, 0);
        }
    }
}

// define led_process_thread with stack size 1024, start running it 100 ms after
// boot
K_THREAD_DEFINE(led_process_tid, 1024, led_process_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);

extern void led_init_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    if (!rgbled_backend_is_ready()) {
        return;
    }

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    // check and indicate battery level on thread start
    LOG_INF("Indicating initial battery status");

    indicate_battery();

    // wait until blink should be displayed for further checks
    k_sleep(K_MSEC(CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS + CONFIG_RGBLED_WIDGET_INTERVAL_MS));
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

    // check and indicate current profile or peripheral connectivity status
    LOG_INF("Indicating initial connectivity status");
    indicate_connectivity();

#if SHOW_LAYER_COLORS
    LOG_INF("Setting initial layer color");
    update_layer_color();
#endif // SHOW_LAYER_COLORS

    initialized = true;

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_KEY_IDLE_OFF)
    reset_key_idle_timeout();
#endif

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_RAINBOW_DEFAULT_ON)
    k_sleep(K_MSEC(CONFIG_RGBLED_WIDGET_CONN_BLINK_MS + CONFIG_RGBLED_WIDGET_INTERVAL_MS +
                   CONFIG_RGBLED_WIDGET_RAINBOW_START_DELAY_MS));
    set_rainbow_enabled(true);
    LOG_INF("Started rainbow after boot indicators");
#endif

    LOG_INF("Finished initializing LED widget");
}

// run init thread on boot for initial battery+output checks
K_THREAD_DEFINE(led_init_tid, 1024, led_init_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 200);
