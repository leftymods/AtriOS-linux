# Station buttons (function / mic mute)

The Station Max has two top-panel buttons behind GPIOs: the "function"
(Alice / voice assistant) button and the microphone mute button.  A plain
`gpio-keys` node is not the right model for them on this board (see below);
the port uses a dedicated polling driver,
`drivers/input/misc/atrios-keypad.c`.

## Vendor implementation

Vendor DTS (`common/arch/arm64/boot/dts/amlogic/sm1_s905x3_yandex.dts`, the
same node is in the decompiled `tools/fw_builder/overlay/dts` copy):

```
gpio_keypad {
	compatible = "amlogic, gpio_keypad";
	status = "okay";
	scan_period = <0x14>;		/* 20 ms */
	key_num = <0x02>;
	key_name = "function", "micmute";
	key_code = <0x246 0xf8>;
	key-gpios = <0x18 0x41 0x01 0x18 0x40 0x01>;
	detect_mode = <0x00>;
};
```

`0x18` is `&gpio`; the pin cells are `0x41` = 65 = `GPIOA_15` (function) and
`0x40` = 64 = `GPIOA_14` (mic mute), both with flag `1` =
`GPIO_ACTIVE_LOW` (see the vendor `meson-g12a-gpio.h`, where
`GPIOA_14 = 64`, `GPIOA_15 = 65`).

Key codes:

| Vendor | Decimal | Linux | Meaning |
|---|---|---|---|
| `0x246` | 582 | `KEY_VOICECOMMAND` | "function"/Alice button |
| `0xf8` | 248 | `KEY_MICMUTE` | microphone mute |

Android userspace confirms the mapping:
`device/yandex/station_max/files/Generic.kl` has `key 582 VOICE_ASSIST`.

**Correction to earlier notes:** 582 is *not* `BTN_TRIGGER_HAPPY7`.
`BTN_TRIGGER_HAPPY7` is `0x2c6` = 710.  The vendor codes are plain `KEY_*`
codes; the driver passes through whatever the DT declares, so a node that
really wants `BTN_TRIGGER_HAPPY7` only needs `key_code = <0x2c6 ...>`.

### What could and could not be verified

- There is **no** `gpio_keypad.ko` / `keypad.ko` in
  `board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/` (51 `.ko`
  files plus module metadata, none of them input related), because the
  driver is built in:
  `CONFIG_AMLOGIC_GPIO_KEY=y` in `common/arch/arm64/configs/meson64_defconfig`.
- The driver source is **not** part of the public Amlogic drop: a grep for
  `gpio_keypad` in `common/` only hits device trees, and
  `drivers/amlogic/input/keyboard/` contains just `adc_keypad.c` and
  `pca9557_keypad.c`.
- No kernel image is available to disassemble either
  (`device/yandex/x3_yandex-kernel/` is absent; `Kernel.mk` refers to a
  prebuilt `Image.gz` that is not in the tree).

So the binding and behaviour below are reconstructed from the DTS plus the
comments other Amlogic device trees give for the same binding
(`detect_mode = <0>; /*0:polling mode, 1:irq mode*/`).  The exact debounce
algorithm and the meaning of `scan_period` (milliseconds vs. jiffies) inside
the vendor driver could not be checked.

## Why not `gpio-keys`

`gpio-keys` is interrupt driven only.  In Linux 7.3
(`drivers/input/keyboard/gpio_keys.c`) every button ends up with an IRQ:
either the `interrupts` property or a `gpiod_to_irq()` on the line; if that
fails, probe fails.  The vendor keypad is a 20 ms poller:

- There is no polling mode, no `scan_period`, no `key_num`/`key_code`
  arrays in `gpio-keys`; every button needs its own child node and IRQ.
  The vendor binding describes a two-line scanning keypad, and the port
  keeps that model so behaviour matches the 4.9 baseline.
- `gpio-keys` has no polarity override (`detect_mode`), and its debounce is
  a timer on the IRQ edge, not "two identical samples" of a periodic scan.
- Polling needs no GPIO interrupt routing at all: it works even if a line
  cannot deliver IRQs through the mainline pinctrl/GPIO controller, and it
  is the only way to reproduce the vendor's "hold and sample" semantics.

The driver therefore implements the vendor binding rather than reusing
`gpio-keys`.

## Mainline driver

`drivers/input/misc/atrios-keypad.c`,
`CONFIG_INPUT_ATRIOS_KEYPAD`, module name `atrios-keypad`, compatibles
`yandex,gpio-keypad` and `amlogic,gpio_keypad` (plus the verbatim vendor
string `"amlogic, gpio_keypad"` with its space, so a stock Station DTB
binds as well).

Properties read (vendor spellings; no dashed aliases):

| Property | Type | Default | Notes |
|---|---|---|---|
| `key-gpios` | GPIO array | required | requested with `GPIOD_IN`, flags from DT |
| `key_num` | u32 | number of `key-gpios` | must match the array size if present |
| `key_code` / `key-codes` | u32 array | `582 248` for <= 2 keys | one code per GPIO, same order |
| `key_name` / `key-names` | string list | none | cosmetic, only logged |
| `scan_period` | u32 | 20 | ms, valid 1..1000, out-of-range falls back to 20 |
| `detect_mode` | u32 | 0 | 0 = use DT flags, 1 = invert (see below) |

Behaviour:

- One `delayed_work` item on `system_wq` samples all lines every
  `scan_period` ms (50 Hz by default, i.e. the vendor's 20 ms).  Lines are
  read with `gpiod_get_value_cansleep()`, which already applies
  `GPIO_ACTIVE_LOW`, so with the Station Max node "pressed" is a logical 1
  when the pin is low.
- Debounce: a new state is only accepted after **two consecutive identical
  samples** (40 ms at the default period).  Nothing is reported for a
  button that is already held when the driver probes.
- Changed keys are reported as `EV_KEY code 1` / `EV_KEY code 0` followed
  by one `EV_SYN`/`SYN_REPORT`.  Each button is a full press/release pair
  per actuation; there is no autorepeat (the vendor driver has none
  either).  `EV_SYN` is enabled by the input core for every device.
- `detect_mode`: other Amlogic DTS files document it as "0: polling mode,
  1: irq mode", and the Station Max uses 0.  The driver always polls, so
  the property is used only as a polarity selector, as agreed for the
  port: `0` keeps the level implied by the DT flags (`GPIO_ACTIVE_LOW` ->
  pressed = low), `1` inverts it (pressed = high).  For the Station Max
  node both readings agree; if a board turns out to be wired the other way,
  either fix the `GPIO_ACTIVE_*` flag in DT or set `detect_mode = <1>`.
- Errors are returned through `dev_err_probe()`; the GPIO array and the
  input device are devres managed.  `remove` stops the work item
  (`cancel_delayed_work_sync()` under the device lock) before devres
  releases the input device, so no scan can run against a dead device.

## Device tree node

This node is already used in `dts/meson-sm1-yandex-station-max.dts`
(replacing the earlier `gpio-keys` draft):
owned by the DTS changes; this text is kept here for reference):

```dts
	gpio-keypad {
		compatible = "yandex,gpio-keypad";
		key_num = <2>;
		key_name = "function", "micmute";
		key_code = <582 248>;	/* KEY_VOICECOMMAND, KEY_MICMUTE */
		key-gpios = <&gpio GPIOA_15 GPIO_ACTIVE_LOW>,
			    <&gpio GPIOA_14 GPIO_ACTIVE_LOW>;
		scan_period = <20>;
		detect_mode = <0>;
	};
```

Note the property names: the driver parses the **vendor** spellings
`key_num`, `key_code`, `key_name`, `scan_period` and `detect_mode`, so they
are used here on purpose.  Dashed aliases (`scan-period`, `detect-mode`,
`key-num`) are not implemented; if the DTS style requires them, the driver
needs a second lookup per property first.  Only the key arrays accept both
forms: `key_code`/`key-codes` and `key_name`/`key-names`.

## Checking

After applying patch `0006-atrios-keypad.patch` and enabling
`CONFIG_INPUT_ATRIOS_KEYPAD`:

```
dmesg | grep -i atrios-keypad
# atrios-keypad gpio-keypad: key 0: function, code 582
# atrios-keypad gpio-keypad: key 1: micmute, code 248
# atrios-keypad gpio-keypad: scanning every 20 ms

cat /proc/bus/input/devices | grep -A4 -i atrios-keypad
ls -l /dev/input/by-path/
evtest /dev/input/eventX
```

Press each button and expect one press/release pair per actuation:

```
Event: time ..., type 1 (EV_KEY), code 582 (KEY_VOICECOMMAND), value 1
Event: time ..., -------------- SYN_REPORT ------------
Event: time ..., type 1 (EV_KEY), code 582 (KEY_VOICECOMMAND), value 0
Event: time ..., -------------- SYN_REPORT ------------
```

and `code 248 (KEY_MICMUTE)` for the mute button.  With the Buildroot image
`evtest` is already enabled (`BR2_PACKAGE_EVTEST=y`), so no extra tooling
is needed on the target.  A quick non-interactive check:

```
evtest --grab /dev/input/eventX &
# or watch the kernel view:
dmesg -w | grep atrios-keypad
```

If the device does not appear at all, check `gpioinfo` for the two lines:
a pinmux/regulator node already claiming `GPIOA_14`/`GPIOA_15` will make
`devm_gpiod_get_array()` fail with `-EBUSY`, which is reported through
`dev_err_probe()`.

## Userspace expectations

- Mic mute: `KEY_MICMUTE` (248).  Clients that follow the standard input
  event codes will see it; a desktop stack would hand it to the mixer.
- Function/Alice button: `KEY_VOICECOMMAND` (582), the code the vendor
  keypad emits and the one Android maps to `VOICE_ASSIST`.  If a specific
  client only listens for `BTN_TRIGGER_HAPPY7` (710), put `0x2c6` in
  `key_code` instead - the driver does not hardcode either value.
- No key layout (`.kl`) file is needed on Linux; the event codes above are
  what `evtest`, `libinput debug-events` and Python `evdev` clients see.
  Desktop stacks that filter rare key codes (some compositors only handle
  the usual keyboard range) may ignore `KEY_VOICECOMMAND`, so a small
  daemon reading `evdev` directly is the safer route for the Alice button.

## Risks

- **Vendor driver unknown.**  No source, no module, no kernel image.  The
  binding is from DTS only; in particular the vendor's debounce and the
  exact unit of `scan_period` could not be confirmed.  Polling at 20 ms is
  assumed to be milliseconds, matching every Amlogic DTS that uses it.
- **`detect_mode` overloading.**  The vendor comment says polling/IRQ mode,
  the port uses it as a polarity selector (per the task agreement).  On the
  Station Max (`detect_mode = 0`, `GPIO_ACTIVE_LOW`) both interpretations
  give the same result, but a board with `detect_mode = 1` in the vendor
  DTS may behave differently than on 4.9.
- **Polling cost.**  A wakeup every 20 ms forever (50 Hz), for two buttons.
  The vendor does the same; if that matters, the period can be raised in
  DT.  No runtime PM/suspend handling: the worker keeps running while the
  system is up, and the buttons are not a wakeup source.
- **DT conflict.**  The current mainline DTS still contains a `gpio-keys`
  node for the same two pins.  Two drivers claiming `GPIOA_14`/`GPIOA_15`
  means one of them fails with `-EBUSY`; only one node must be present.
  `gpio-keys` also requires IRQs, which this board's vendor binding does
  not use.
- **Key code mismatch.**  Any documentation that calls 582
  `BTN_TRIGGER_HAPPY7` is wrong (that is 710).  Userspace written against
  the wrong code will not react; align on `KEY_VOICECOMMAND` or change
  `key_code` in DT.
- **No autorepeat / no long-press.**  Matches the vendor driver, but a
  long press of the function button produces exactly one press/release.
