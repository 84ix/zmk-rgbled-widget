# zmk-rgbled-widget PWM化＋レインボーボタン追加 実装要件

## 目的

`caksoylar/zmk-rgbled-widget` を fork し、XIAO BLE / Seeeduino XIAO nRF52840 のオンボードRGB LEDを、従来のGPIO ON/OFF制御だけでなくPWM制御でも扱えるようにする。

最終的には、ZMK keymap から次のような behavior をマップできるようにする。

```dts
&ind_rainbow
```

これにより、キー押下時にXIAO BLE本体のRGB LEDを一定時間レインボー表示させる。

## 背景

現在使用している `zmk-rgbled-widget` は、XIAO BLEなどに搭載されている3色LEDを使って、バッテリー状態、BLE接続状態、レイヤー状態などを表示するZMK moduleである。

ただし現状の設計は、基本的に `gpio-leds` による3本のGPIO ON/OFF制御を前提としている。

そのため、現状では次のような段階色は出せる。

```text
赤 / 緑 / 青 / 黄 / 水色 / 紫 / 白 / 消灯
```

一方で、滑らかなレインボー表示には、R/G/Bそれぞれの明るさを連続的に変えるPWM制御が必要になる。

## 対象リポジトリ

fork元:

```text
https://github.com/caksoylar/zmk-rgbled-widget
```

想定fork先:

```text
https://github.com/hashiguchi-kazuhiro/zmk-rgbled-widget
```

実際のGitHubアカウント名に合わせて変更すること。

## 実装方針

既存のGPIO版を壊さず、PWM版を追加する。

つまり、既存ユーザー向けの `gpio-leds` バックエンドは維持し、設定でPWMバックエンドへ切り替えられるようにする。

想定するKconfig:

```conf
CONFIG_RGBLED_WIDGET=y

# 既存互換: GPIO backend
CONFIG_RGBLED_WIDGET_BACKEND_GPIO=y

# 新規: PWM backend
CONFIG_RGBLED_WIDGET_BACKEND_PWM=n

# 新規: rainbow behavior
CONFIG_RGBLED_WIDGET_RAINBOW=y
CONFIG_RGBLED_WIDGET_RAINBOW_DURATION_MS=1200
CONFIG_RGBLED_WIDGET_RAINBOW_INTERVAL_MS=20
CONFIG_RGBLED_WIDGET_RAINBOW_PERIOD_MS=8
```

ただし、ZMK/ZephyrのKconfig上でchoiceにした方が自然なら、次のような形でもよい。

```conf
choice RGBLED_WIDGET_BACKEND
    default RGBLED_WIDGET_BACKEND_GPIO

config RGBLED_WIDGET_BACKEND_GPIO
    bool "GPIO backend"

config RGBLED_WIDGET_BACKEND_PWM
    bool "PWM backend"
    select PWM
endchoice
```

## ハードウェア前提

XIAO BLE / Seeeduino XIAO nRF52840 のオンボードRGB LEDを使う。

対象ピン:

| 色 | nRF52840 pin | 備考 |
|---|---:|---|
| Red | P0.26 | active low |
| Green | P0.30 | active low |
| Blue | P0.06 | active low |

このRGB LEDは active low なので、PWM制御では反転極性を使う。

## PWMピンについて

nRF52840のPWMは、専用PWMピン固定ではなく、PWM出力を任意のGPIOへPSELで割り当てる方式である。

したがって、P0.26 / P0.30 / P0.06 もPWM出力先として使える想定でよい。

実装時はZephyr Devicetreeで `pwm0` の3chを次のように割り当てる。

- PWM_OUT0 -> P0.26
- PWM_OUT1 -> P0.30
- PWM_OUT2 -> P0.06

## Devicetree案

XIAO BLE用に、新しいPWM adapter shieldを追加する。

既存の `rgbled_adapter` はGPIO版として維持し、新規に以下を追加する。

```text
boards/shields/rgbled_pwm_adapter/
├── Kconfig.shield
├── Kconfig.defconfig
└── rgbled_pwm_adapter.overlay
```

### rgbled_pwm_adapter.overlay 案

```dts
#include <dt-bindings/pwm/pwm.h>
#include <dt-bindings/pinctrl/nrf-pinctrl.h>

/ {
    pwmleds {
        compatible = "pwm-leds";

        red_pwm_led: red_pwm_led {
            pwms = <&pwm0 0 PWM_MSEC(1) PWM_POLARITY_INVERTED>;
            label = "RED_PWM_LED";
        };

        green_pwm_led: green_pwm_led {
            pwms = <&pwm0 1 PWM_MSEC(1) PWM_POLARITY_INVERTED>;
            label = "GREEN_PWM_LED";
        };

        blue_pwm_led: blue_pwm_led {
            pwms = <&pwm0 2 PWM_MSEC(1) PWM_POLARITY_INVERTED>;
            label = "BLUE_PWM_LED";
        };

        aliases {
            led-red-pwm = &red_pwm_led;
            led-green-pwm = &green_pwm_led;
            led-blue-pwm = &blue_pwm_led;
        };
    };
};

&pwm0 {
    status = "okay";
    pinctrl-0 = <&pwm0_default>;
    pinctrl-1 = <&pwm0_sleep>;
    pinctrl-names = "default", "sleep";
};

&pinctrl {
    pwm0_default: pwm0_default {
        group1 {
            psels = <
                NRF_PSEL(PWM_OUT0, 0, 26)  /* Red   P0.26 */
                NRF_PSEL(PWM_OUT1, 0, 30)  /* Green P0.30 */
                NRF_PSEL(PWM_OUT2, 0, 6)   /* Blue  P0.06 */
            >;
        };
    };

    pwm0_sleep: pwm0_sleep {
        group1 {
            psels = <
                NRF_PSEL(PWM_OUT0, 0, 26)
                NRF_PSEL(PWM_OUT1, 0, 30)
                NRF_PSEL(PWM_OUT2, 0, 6)
            >;
            low-power-enable;
        };
    };
};
```

注意:
このoverlayはたたき台である。ZMK/Zephyrのバージョンによって `aliases` の置き場所、`label` の扱い、`PWM_MSEC()` の使用可否などで調整が必要になる可能性がある。

## build.yaml設定例

PWM版を使う場合、既存の `rgbled_adapter` ではなく `rgbled_pwm_adapter` を使う。

```yaml
---
include:
  - board: seeeduino_xiao_ble
    shield: your_keyboard_left rgbled_pwm_adapter
  - board: seeeduino_xiao_ble
    shield: your_keyboard_right rgbled_pwm_adapter
```

## prj.conf / shield conf 設定例

```conf
CONFIG_RGBLED_WIDGET=y
CONFIG_RGBLED_WIDGET_BACKEND_PWM=y
CONFIG_RGBLED_WIDGET_RAINBOW=y

CONFIG_PWM=y
CONFIG_PINCTRL=y

CONFIG_RGBLED_WIDGET_RAINBOW_DURATION_MS=1200
CONFIG_RGBLED_WIDGET_RAINBOW_INTERVAL_MS=20
```

`CONFIG_LED_PWM` は、ZephyrのLED PWMドライバ経由で実装する場合のみ必要。  
自前で `pwm_set_dt()` を使う場合は不要な可能性がある。

```conf
# LED PWM driver経由にする場合だけ
CONFIG_LED=y
CONFIG_LED_PWM=y
```

## C実装方針

### 1. LED制御層を分離する

既存コードのGPIO制御部分を、バックエンド抽象化する。

イメージ:

```c
void rgbled_widget_set_color_u8(uint8_t r, uint8_t g, uint8_t b);
void rgbled_widget_off(void);
```

GPIOバックエンドでは、`r/g/b` を閾値でON/OFFに丸める。

```c
static void rgbled_widget_set_gpio_u8(uint8_t r, uint8_t g, uint8_t b)
{
    bool red_on = r > 0;
    bool green_on = g > 0;
    bool blue_on = b > 0;

    /* active low / GPIO_ACTIVE_LOWはgpio_dt_spec側に任せる */
    gpio_pin_set_dt(&red_gpio, red_on);
    gpio_pin_set_dt(&green_gpio, green_on);
    gpio_pin_set_dt(&blue_gpio, blue_on);
}
```

PWMバックエンドでは、0-255をdutyへ変換する。

```c
static void rgbled_widget_set_pwm_u8(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t period = PWM_MSEC(CONFIG_RGBLED_WIDGET_RAINBOW_PERIOD_MS);

    pwm_set_dt(&red_pwm, period, (period * r) / 255);
    pwm_set_dt(&green_pwm, period, (period * g) / 255);
    pwm_set_dt(&blue_pwm, period, (period * b) / 255);
}
```

`PWM_POLARITY_INVERTED` をDevicetree側に入れる前提で、C側では通常通り `0=消灯, 255=最大輝度` として扱う。

### 2. Devicetree取得

PWM版では `PWM_DT_SPEC_GET()` を使う。

例:

```c
#include <zephyr/drivers/pwm.h>

#define RED_PWM_NODE DT_ALIAS(led_red_pwm)
#define GREEN_PWM_NODE DT_ALIAS(led_green_pwm)
#define BLUE_PWM_NODE DT_ALIAS(led_blue_pwm)

static const struct pwm_dt_spec red_pwm = PWM_DT_SPEC_GET(RED_PWM_NODE);
static const struct pwm_dt_spec green_pwm = PWM_DT_SPEC_GET(GREEN_PWM_NODE);
static const struct pwm_dt_spec blue_pwm = PWM_DT_SPEC_GET(BLUE_PWM_NODE);
```

alias名はZephyrの命名規則により `led-red-pwm` が `DT_ALIAS(led_red_pwm)` になる点に注意。

### 3. 初期化

PWMデバイスがreadyか確認する。

```c
if (!pwm_is_ready_dt(&red_pwm) ||
    !pwm_is_ready_dt(&green_pwm) ||
    !pwm_is_ready_dt(&blue_pwm)) {
    LOG_ERR("RGB PWM device not ready");
    return -ENODEV;
}
```

GPIO版では既存処理を維持する。

### 4. HSV -> RGB変換

レインボー表示ではHSVのHueを時間で回し、RGBに変換する。

浮動小数点は避け、整数演算で実装する。

簡易実装例:

```c
static void hsv_to_rgb_u8(uint8_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region = hue / 43;
    uint8_t remainder = (hue - (region * 43)) * 6;

    uint8_t p = 0;
    uint8_t q = 255 - remainder;
    uint8_t t = remainder;

    switch (region) {
    case 0:
        *r = 255; *g = t;   *b = p;   break;
    case 1:
        *r = q;   *g = 255; *b = p;   break;
    case 2:
        *r = p;   *g = 255; *b = t;   break;
    case 3:
        *r = p;   *g = q;   *b = 255; break;
    case 4:
        *r = t;   *g = p;   *b = 255; break;
    default:
        *r = 255; *g = p;   *b = q;   break;
    }
}
```

必要なら、まぶしすぎる場合に明度を落とす。

```conf
CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS=64
```

として、RGB値にスケールを掛ける。

```c
r = (r * CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS) / 255;
g = (g * CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS) / 255;
b = (b * CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS) / 255;
```

### 5. work queueでレインボー更新

ZMK上でブロックしないよう、`k_work_delayable` を使って周期更新する。

```c
static struct k_work_delayable rainbow_work;
static int64_t rainbow_end_at;
static uint8_t rainbow_hue;

static void rainbow_work_handler(struct k_work *work)
{
    int64_t now = k_uptime_get();

    if (now >= rainbow_end_at) {
        rgbled_widget_restore_or_off();
        return;
    }

    uint8_t r, g, b;
    hsv_to_rgb_u8(rainbow_hue, &r, &g, &b);
    rgbled_widget_set_color_u8(r, g, b);

    rainbow_hue += 4;

    k_work_schedule(&rainbow_work, K_MSEC(CONFIG_RGBLED_WIDGET_RAINBOW_INTERVAL_MS));
}

void rgbled_widget_start_rainbow(void)
{
    rainbow_end_at = k_uptime_get() + CONFIG_RGBLED_WIDGET_RAINBOW_DURATION_MS;
    rainbow_hue = 0;

    k_work_cancel_delayable(&rainbow_work);
    k_work_schedule(&rainbow_work, K_NO_WAIT);
}
```

### 6. 既存表示との優先順位

既存のバッテリー/BLE/レイヤー表示と競合する可能性がある。

最小実装では、`&ind_rainbow` 実行中はレインボーが最優先でLEDを占有する。

レインボー終了後は、まず消灯でよい。

将来的には「直前のlayer表示へ戻す」などを実装してもよいが、初回実装では不要。

優先順位案:

```text
1. 手動表示: &ind_rainbow / &ind_bat / &ind_con
2. 重要通知: critical battery
3. layer表示
4. 消灯
```

ただし初回は、既存処理を壊さないために `&ind_rainbow` behaviorだけを追加し、全キー押下検知は入れない。

## Behavior追加

既存の `&ind_bat` / `&ind_con` と同じ方式で、次を追加する。

```dts
&ind_rainbow
```

### dtsi追加案

`include/behaviors/rgbled_widget.dtsi` または既存のbehavior dtsiに追記する。

```dts
/ {
    behaviors {
        ind_rainbow: ind_rainbow {
            compatible = "zmk,behavior-rgbled-rainbow";
            #binding-cells = <0>;
        };
    };
};
```

### binding yaml追加案

```text
dts/bindings/behaviors/zmk,behavior-rgbled-rainbow.yaml
```

内容例:

```yaml
description: RGB LED rainbow indicator behavior

compatible: "zmk,behavior-rgbled-rainbow"

include: zero_param.yaml
```

### behavior C実装案

```c
static int behavior_rgbled_rainbow_binding_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event)
{
    rgbled_widget_start_rainbow();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_rgbled_rainbow_binding_released(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event)
{
    return ZMK_BEHAVIOR_OPAQUE;
}
```

既存behavior実装の書き方に合わせること。

## keymap使用例

### レインボー専用キー

```dts
#include <behaviors/rgbled_widget.dtsi>

/ {
    keymap {
        compatible = "zmk,keymap";

        default_layer {
            bindings = <
                &kp Q        &kp W        &kp E        &kp R
                &ind_rainbow &kp A        &kp S        &kp D
            >;
        };
    };
};
```

### 通常キーと同時に光らせる例

ZMK macroを使う。

```dts
#include <behaviors/rgbled_widget.dtsi>

/ {
    macros {
        a_with_rainbow: a_with_rainbow {
            compatible = "zmk,behavior-macro";
            #binding-cells = <0>;
            bindings = <&ind_rainbow>, <&kp A>;
        };
    };
};
```

keymap:

```dts
&a_with_rainbow
```

## 将来拡張: 任意キー押下で自動レインボー

初回実装では不要。  
安定したら、Kconfigで有効化できるオプションとして追加する。

```conf
CONFIG_RGBLED_WIDGET_RAINBOW_ON_KEYPRESS=y
```

この場合、ZMKのキーイベントを購読して、押下時に `rgbled_widget_start_rainbow()` を呼ぶ。

想定:

```c
ZMK_LISTENER(rgbled_keypress, rgbled_keypress_listener);
ZMK_SUBSCRIPTION(rgbled_keypress, zmk_keycode_state_changed);
```

ただし、全キー押下でLEDを光らせると電池消費が増えるため、初回実装ではbehavior方式を優先する。

## ファイル変更一覧

想定される変更・追加ファイル:

```text
Kconfig
CMakeLists.txt

src/
  rgbled_widget.c                  # 既存LED制御層の抽象化
  rgbled_backend_gpio.c            # 必要なら分離
  rgbled_backend_pwm.c             # 新規
  behavior_rgbled_rainbow.c        # 新規

include/
  zmk_rgbled_widget/
    rgbled_widget.h                # start_rainbow等の宣言
    rgbled_backend.h               # 必要なら追加

dts/
  bindings/
    behaviors/
      zmk,behavior-rgbled-rainbow.yaml

include/
  behaviors/
    rgbled_widget.dtsi             # &ind_rainbow追加

boards/
  shields/
    rgbled_pwm_adapter/
      Kconfig.shield
      Kconfig.defconfig
      rgbled_pwm_adapter.overlay
```

既存リポジトリの構成に合わせてファイル名は調整してよい。

## 実装ステップ

### Step 1: forkしてブランチ作成

```bash
git clone https://github.com/hashiguchi-kazuhiro/zmk-rgbled-widget.git
cd zmk-rgbled-widget
git checkout -b feature/pwm-rainbow
```

### Step 2: Kconfig追加

- `CONFIG_RGBLED_WIDGET_BACKEND_GPIO`
- `CONFIG_RGBLED_WIDGET_BACKEND_PWM`
- `CONFIG_RGBLED_WIDGET_RAINBOW`
- `CONFIG_RGBLED_WIDGET_RAINBOW_DURATION_MS`
- `CONFIG_RGBLED_WIDGET_RAINBOW_INTERVAL_MS`
- `CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS`

### Step 3: PWM adapter shield追加

`boards/shields/rgbled_pwm_adapter` を追加する。

まずXIAO BLE専用でよい。

### Step 4: PWM backend追加

`pwm_set_dt()` でR/G/Bのdutyを変更する関数を追加する。

### Step 5: 既存GPIO backendを維持

既存のGPIO制御が壊れないようにする。  
PWM未使用時は従来通りビルド・動作すること。

### Step 6: レインボーwork実装

`k_work_delayable` で一定時間だけHSV hueを回す。

### Step 7: `&ind_rainbow` behavior追加

keymapから呼べるbehaviorを追加する。

### Step 8: ZMK config側で試す

`west.yml` をfork先に変更。

```yaml
manifest:
  remotes:
    - name: hashiguchi-kazuhiro
      url-base: https://github.com/hashiguchi-kazuhiro

  projects:
    - name: zmk-rgbled-widget
      remote: hashiguchi-kazuhiro
      revision: feature/pwm-rainbow
```

`build.yaml` で `rgbled_pwm_adapter` を追加。

```yaml
include:
  - board: seeeduino_xiao_ble
    shield: your_keyboard rgbled_pwm_adapter
```

keymapに `&ind_rainbow` を配置。

## 受け入れ条件

### GPIO互換

- 既存の `rgbled_adapter` を使ったビルドが通ること
- 既存の `&ind_bat` / `&ind_con` 等が壊れていないこと

### PWM版

- `rgbled_pwm_adapter` を使ったビルドが通ること
- XIAO BLEオンボードRGB LEDのR/G/Bが個別にPWM制御できること
- active lowのため、消灯・点灯の極性が正しいこと
- `&ind_rainbow` を押すと一定時間レインボー表示されること
- レインボー終了後にLEDが消灯すること

### 省電力

- レインボーしていないときはPWM duty 0、または完全消灯状態になること
- 全キー押下で自動点灯する機能は初回実装では入れない

## デバッグ用設定

必要ならログを追加する。

```conf
CONFIG_LOG=y
CONFIG_RGBLED_WIDGET_LOG_LEVEL_DBG=y
```

ログ例:

```text
RGBLED: PWM backend initialized
RGBLED: rainbow start
RGBLED: rainbow stop
RGBLED: PWM device not ready
```

## 注意点

1. XIAO BLEのオンボードRGB LEDはactive low。
2. 既存のGPIO版とPWM版のDevicetree定義を同時に有効化しない。
3. PWM版では `rgbled_adapter` ではなく `rgbled_pwm_adapter` を使う。
4. `PWM_POLARITY_INVERTED` を使う。
5. ZMK/Zephyrバージョンによってpinctrlやpwm-ledsの書式調整が必要になる可能性がある。
6. 最初から全キー押下連動を入れず、まずは `&ind_rainbow` behaviorで実装する。
7. まぶしすぎる場合に備えて、`CONFIG_RGBLED_WIDGET_RAINBOW_BRIGHTNESS` を用意する。

## Codexへの実装指示

以下の方針で実装してください。

```text
caksoylar/zmk-rgbled-widget をベースに、既存GPIO制御を壊さずPWM backendを追加してください。

対象はSeeeduino XIAO BLE / nRF52840のオンボードRGB LEDです。
Red=P0.26, Green=P0.30, Blue=P0.06, active lowです。

新規shieldとして rgbled_pwm_adapter を追加し、pwm0のOUT0/OUT1/OUT2をそれぞれP0.26/P0.30/P0.06へ割り当ててください。

KconfigでGPIO backendとPWM backendを切り替えられるようにしてください。
PWM backend有効時は pwm_set_dt() でRGB各色を0-255 duty制御してください。

新規behavior &ind_rainbow を追加してください。
&ind_rainbow が押されたら、k_work_delayableで一定時間HSV hueを回してレインボー表示し、終了後にLEDを消灯してください。

初回実装では、任意キー押下で自動レインボーする機能は不要です。
既存の &ind_bat / &ind_con などが壊れないようにしてください。
```
