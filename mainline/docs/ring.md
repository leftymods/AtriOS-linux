# LED ring (24 × RGB)

## Vendor implementation

Two IS31FL3236A controllers on the vendor I2C bus `i2c@1f000`:

| Chip | Address | Channels | Controls |
|---|---|---|---|
| left | `0x3c` | 36 | ring LEDs 1-12 |
| right | `0x3f` | 36 | ring LEDs 13-24 |

Loaded modules:

- `leds-is31fl32xx.ko` — **the upstream driver** (`drivers/leds/leds-is31fl32xx.c`),
  rebuilt with two vendor additions:
  - compatible `issi,is31fl3236,gva`
  - child property `reg_offset` instead of `reg`
  - property `max-brightness-scale` (DT value `200`) and `default-max-brightness`
- `quasar_led_ring.ko` — source path `led_ring_fb.c`; registers:
  - framebuffer `/dev/fb5`, 72 bytes, RGB888 (24 ring pixels × 3 bytes)
  - backlight ops
  - LED trigger **`led_ring_fb`**
  - activation is refused for an LED whose `max_brightness != 200`
    (`cannot activate led_ring_fb trigger for led with max_brightness != %d`)

DT shape (per chip, 36 children):

```
led1_r { reg_offset = <0x01>; label = "station2:red:ring1";    linux,default-trigger = "led_ring_fb"; };
led1_g { reg_offset = <0x02>; label = "station2:green:ring1";  ... };
led1_b { reg_offset = <0x03>; label = "station2:blue:ring1";   ... };
...
led12_b { reg_offset = <0x24>; label = "station2:blue:ring12"; ... };
```

Userspace writes frames straight to `/dev/fb5`:

```python
with open("/dev/fb5", "wb") as f:   # 72 bytes
    f.write(bytes([r, g, b]) * 24)
```

Channel order in the frame is LED-major: `ring1.r, ring1.g, ring1.b, ring2.r, …`.
The two chips are therefore one flat 72-channel device from userspace's point
of view; `/usr/lib/atrios/light` (ring_fill/ring_volume/ring_rainbow) is the
only writer.

## Mainline port

Two pieces:

1. **Driver patch** for upstream `leds-is31fl32xx`:
   - accept `issi,is31fl3236,gva` with the IS31FL3236 channel map
   - fall back to `reg_offset` when `reg` is absent
   - honour `max-brightness-scale` as `led_classdev.max_brightness`
   This keeps the 72 LED class devices with the same labels as the vendor
   kernel, so `/sys/class/leds/station2:*:ring*` is unchanged.

2. **New trigger driver** `drivers/leds/trigger/ledtrig-ringfb.c`:
   - platform device `yandex,led-ring-fb`
   - fbdev 24×1, 24 bpp (72 bytes) with deferred IO
   - on buffer update: `led_set_brightness()` for the 72 channels, sorted by
     label so the flat order does not depend on I2C probe order
   - registers the `led_ring_fb` trigger; activation collects ring channels
     (`max_brightness == 200`), deactivation clears them
   - a `backlight` device is not needed: `/usr/lib/atrios/light` only writes
     the framebuffer

Result: `/usr/lib/atrios/light` and the volume/rainbow animations work on
mainline without changes.

## Evidence

- DTB `sm1.dtb` sha256 `f70a9b41942912434d2708318f595e8fe65a96f5d533dad8fe279237af8acc91`
- REA evidence for the DTB: `mainline/docs/evidence/dtb-summary.json`
- module strings: `led_ring_fb.c`, `cannot activate led_ring_fb trigger for led
  with max_brightness != %d`, `failed to allocate video memory`
- `nm quasar_led_ring.ko`: `led_ring_trigger_activate`, `led_ring_trigger_deactivate`,
  `fb_ops`, `bl_ops`, `active_fb`
