# Rotary volume encoder

## Vendor implementation

`rotary-volume.ko` (kernel 4.9.113, aarch64, not stripped) is a small
platform driver.  Reversed from the module:

```
compatible = "rotary-volume"
gpios      = two GPIO descriptors, target "gpios"
key-codes  = two u32 cells
```

Relevant symbols: `rotary_volume_probe`, `rotary_volume_irq`,
`rotary_volume_get_state` (inlined), `rotary_volume_work`,
`rotary_volume_report_event` (outlined constprop clone, called by the work
item).

Algorithm (confirmed by `objdump -dr` and DWARF line info):

1. Probe requires exactly two GPIOs (`devm_gpiod_get_array`) and exactly two
   `key-codes`.  It registers an input device with bustype 25 (`BUS_HOST`; the vendor
   header calls it `BUS_I8046`),
   `EV_KEY` capabilities for both codes.
2. One IRQ is requested per GPIO with flags `IRQF_TRIGGER_FALLING` only
   (`mov w4, #2` at `rotary_volume_probe+0x1ac`).  The hard handler just
   queues `rotary_volume_work` on `system_wq` and returns `IRQ_HANDLED`.
3. The work item samples both lines with `gpiod_get_value_cansleep()` and
   builds a two-bit state, bit 0 = first GPIO, bit 1 = second GPIO.
4. `state == 01` reports `key-codes[0]`; `state == 10` reports
   `key-codes[1]`; `00` and `11` are ignored.
5. `rotary_volume_report_event()` sends `EV_KEY code 1`, `EV_KEY code 0`,
   then `EV_SYN/SYN_REPORT 0` — one complete click per detent.

There is **no** state history, debounce, timer or autorepeat.  For the
encoder on this board the rest state is `11`: a detent is one line falling
while the other stays high, so exactly one of the two falling edges per
mechanical step matches `01` or `10`; the other samples `00`.

`key-codes = <KEY_PAGEUP KEY_PAGEDOWN>` (104/109, what `rotaryd` reads) gives:

| State | First GPIO (GPIOC_7) | Second GPIO (GPIOAO_10) | Event |
|---|---|---|---|
| `01` | high | low | `KEY_PAGEUP` |
| `10` | low | high | `KEY_PAGEDOWN` |
| `00`, `11` | — | — | none |

The vendor module also has no `remove` callback and never calls
`cancel_work_sync()`, so an IRQ/work item can outlive the device data on
unbind.  The mainline driver does not repeat that.

## Mainline driver

`drivers/input/misc/rotary-volume.c`, selected by `CONFIG_INPUT_ROTARY_VOLUME`,
compatible with both `yandex,rotary-volume` and the vendor string
`rotary-volume`.

Differences from the vendor module:

- `devm_gpiod_get_array()` + `gpiod_to_irq()` +
  `devm_request_threaded_irq()`.  The IRQ is threaded because GPIO lines can
  sit behind a sleeping controller (`gpiod_get_value_cansleep()`).
- IRQs are requested on **both** edges (`IRQF_TRIGGER_RISING |
  IRQF_TRIGGER_FALLING | IRQF_ONESHOT`) so the full quadrature sequence is
  observed.  A detent is still defined as a line falling while the other is
  high; the previous state is stored, so the `00 -> 10` / `00 -> 01` rising
  edges cannot produce a duplicate event.
- A 5 ms timestamp debounce suppresses contact bounce.  It is deliberately
  short: the falling-edge+other-line-high rule already yields one event per
  detent, and 5 ms is well below the interval of a fast turn.
- `key-codes` is optional; without it the driver defaults to
  `KEY_PAGEUP`/`KEY_PAGEDOWN` (104/109), matching `rotaryd`.
- The input device is unregistered through a devres action added before the
  IRQs, so devres frees the IRQs first and the input device cannot receive
  events after it has been unregistered.
- No workqueue; the threaded handler runs the decode directly.

## DT example

Already present in `dts/meson-sm1-yandex-station-max.dts`:

```dts
rotary-volume {
	compatible = "yandex,rotary-volume";
	gpios = <&gpio GPIOC_7 GPIO_ACTIVE_HIGH>,
		<&gpio_ao GPIOAO_10 GPIO_ACTIVE_HIGH>;
	key-codes = <KEY_PAGEUP KEY_PAGEDOWN>;  /* 104/109, what rotaryd reads */
};
```

`KEY_PAGEUP`/`KEY_PAGEDOWN` would also work without a driver change; the
driver uses whatever order is in `key-codes`.

## Why not upstream `rotary-encoder`?

`drivers/input/misc/rotary_encoder.c` is a relative-axis device: it reports
`EV_REL/REL_X` (or a configurable axis) and expects userspace to translate
motion into volume changes.  The Station userspace does not run such a
daemon and listens for key events instead.  It also uses
`linux,axis`/`rotary-encoder,steps-per-period` and a different DT binding,
while the vendor DTB and `/usr/lib/atrios` expect a two-key input device.
A small dedicated driver keeps the vendor ABI (device name `rotary-volume`,
instant press+release key events) and needs no userspace change.

## Verification notes / risks

- Patch only: no kernel build was run in this environment.  Verified with
  `patch -p1 --dry-run` against a Linux 7.3 tree (see the patch header).
- `/usr/lib/atrios/rotaryd` reacts to `KEY_PAGEUP` (104) and
  `KEY_PAGEDOWN` (109); the vendor DT says the same and the mainline DTS is
  generated with those codes, so no userspace change is needed.
- The decode assumes the encoder rests at `11` and that a detent is a single
  line falling while the other is high, i.e. the same assumption the vendor
  driver makes implicitly.  A different encoder wiring may need the initial
  state / polarity adjusted.
- A GPIO controller that cannot generate both-edge interrupts would fail at
  request time.  The vendor driver used falling edges only; if a given
  kernel's Amlogic GPIO/AO irqchip rejects both edges, drop
  `IRQF_TRIGGER_RISING` — the decode still works, because it only emits on a
  1 -> 0 transition.
