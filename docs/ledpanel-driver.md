# Front LED panel driver (25x16)

`drivers/video/fbdev/atrios-ledpanel.c`, `CONFIG_FB_ATRIOS_LEDPANEL`,
module `atrios-ledpanel`, patch `patches/linux/0005-atrios-ledpanel.patch`.

The panel is a 25x16 white LED matrix behind the front grille, driven by a
Gowin FPGA on `spicc1`. Userspace (`/usr/lib/atrios/screend`) writes
`25*16*3 = 1200` bytes of RGB888 to `/dev/fb4` and closes the file; the driver
reduces every pixel to one luminance byte (`max(r,g,b)`) and sends a 400-byte
frame over SPI.

Related: `docs/screen.md` has the full reverse engineering of both vendor
modules (fbdev internals, JTAG, FPGA reflash, touch/UART expander). This file
only covers what the mainline driver implements.

## Two firmwares, one DT node

The vendor DT has a single node with two compatibles:

```
panel@0 {
        compatible = "yandex,led-screen", "atri,led-panel";
        reg = <0>;
        spi-max-frequency = <4000000>;
        max-brightness = <200>;
        reset-gpios = <&gpio GPIOX_8 GPIO_ACTIVE_LOW>;
};
```

Two vendor modules can bind to it, and they speak **different application
protocols**:

| | `gowin_led_device.ko` ("PA") | `quasar_led_screen.ko` ("SA") |
|---|---|---|
| binds on this board | yes (`"ya,led_screen"` → `led_device_id`) | no (compatible `"quasar_led_screen"`) |
| status exchange | 35 B pure read (`tx_buf = NULL`) | 20 B payload of zeros + CRC |
| CRC of response | residual: `crc_itu_t(0, buf, 35) == 0` | `le16(buf[14..15]) == crc_itu_t(0, buf, 14)` |
| `SHOW_PIC` | `07 00 <delay u16 le> <image>` | `07 <image> <delay u16 le>` |
| geometry | `board_rev` lookup table in the driver | `xres`/`yres` in the response |
| status field | bit mask, byte 16 | value, byte 8 (`21` = CRC error) |
| write CRC | always | only for API version 9 |

Both use the same physical framing, the same 400-byte luminance payload and the
same `SET_BRIGHTNESS` packet, so the driver implements both and picks one at
probe time. With `proto=` the choice can be forced (0 = PA, 1 = SA,
-1 = autodetect, the default).

## Recovered protocol

### Framing (both)

One command = one SPI transfer, mode 0, 8 bit/word, CS asserted for the whole
command. Last two bytes are a CRC-16/XMODEM (`poly 0x1021`, `init 0`, no
reflection, no final xor) over everything before them, sent **MSB first**.

Evidence (`quasar_led_screen.ko`, `sa_write_v9` at `.text+0x23d0`):

```
2410: bl   crc_itu_t                  ; crc_itu_t(0, buf, len)
2424: rev16 w0, w0                    ; byte swap
2430: strh w0, [x20, x19]             ; buf[len] = hi, buf[len+1] = lo
243c: add  x19, x19, #0x2             ; len += 2
2480: bl   spi_sync                   ; one transfer, tx_buf = buf
```

`gowin_led_device.ko` (`lp_write` at `.text+0x4a88`) does the same and then
always issues `get_lp_status()` as a second transfer.

### Status read: SA

`sa_none_unsafe` (`.text+0x2158`) builds `20 * 0x00` + CRC and reads 20 bytes
back (`mov x2,#0x14`). `sa_parse_response` (`.text+0x2258`) layout:

| byte | field | how it is read |
|---|---|---|
| 0 | api_version | `ldrb` → `resp->info.api_version` |
| 1 | xres | `ldrb` |
| 2 | yres | `ldrb` |
| 3 | pixel_depth | `ldrb` |
| 4 | frame_queue_overflows | `ldrb` |
| 5 | frames_in_frame_queue | `ldrb` |
| 6 | temp_celsius | `ldrb` |
| 7 | fw_version | `ldrb` |
| 8 | last_cmd_status (u32, low byte on the wire) | `ldrb` + `str w` |
| 9..13 | reserved | two overlapping `ldur w` copies |
| 14..15 | crc16 | `ldrh w0,[x19,#14]`, compared to `crc_itu_t(0,buf,14)` |

Note the asymmetry: the command CRC is big endian, the response CRC is
compared as a plain little endian `ldrh`. The driver reproduces that.

`struct sa_info`/`struct sa_response` sizes (20 B) were confirmed with
`pahole` on the module's DWARF.

### Status read: PA

`get_lp_status` (`.text+0x4598`): `struct spi_transfer { tx_buf = NULL,
rx_buf = buf, len = 35 }`, then

```
46e4: bl   crc_itu_t                  ; crc_itu_t(0, buf, 35)
46e8: tst  w0, #0xffff
46ec: b.eq ...                        ; == 0 means OK (residual check)
```

`lp_write` then inspects `resp.last_cmd_status` (= `buf[16]`, read at
`[x29,#109]`):

```
4b6c: ldrb w28, [x29, #109]
4b70: tbnz w28, #2, ...               ; CMD_OVF   -> retry
4b74: tbz  w28, #1, ...               ; CRC_ERROR -> retry
4b84: cbnz w28, ...                   ; any other bit -> error
```

Field offsets used by the driver: `buf[1]` = application mode (must be 1,
`"application mode number mismatch: expected=0x%02hhx, actual=0x%02hhx"`),
`buf[2]` = fw_version, `buf[3..4]` = board_rev, `buf[15]` = frames_in_queue,
`buf[16]` = status mask. Bit 3 (`FRM_OVF`) is masked out by the vendor and is
not treated as an error.

### Frame

`sa_show_pic` (`.text+0x2e38`):

```
2e60: mov  w0, #0x7
2e64: strb w0, [x19, #17]             ; cmd_buf[0] = 0x07
2e70: add  x0, x19, #0x12             ; cmd_buf + 1
2e74: bl   memcpy                     ; image
2e78: add  x0, x19, x20               ; cmd_buf + len
2e84: strh w21, [x0, #18]             ; u16 delay at cmd_buf[len+1]
2e8c: ldr  x3, [x3, #8]               ; fw_impl->ops.sa_write
2e90: blr  x3
```

`show_pic_app_cmd` (gowin, `.text+0x4ee0`):

```
4f30: mov  w0, #0x7
4f3c: strb w0, [x19]                  ; buf[0] = 0x07
4f40: strb wzr, [x19, #1]             ; buf[1] = 0x00
4f48: strh w23, [x19, #2]             ; u16 delay at buf[2]
4f44: add  x0, x19, #0x4
4f4c: bl   memcpy                     ; image at buf[4]
4f54: mov  x2, x21                    ; len = image + 6
4f60: bl   lp_write
```

Payload = `xres*yres` bytes of luminance, row major, x fastest.
`qls_fb_sync` (`.text+0x858`) computes it per pixel:

```
; pixel = b[0] | b[1]<<8 | b[2]<<16   (red at bit 16, blue at bit 0)
lsr/and  -> r, g, b            ; masked with the fb_var bitfield length
mul by 255 / udiv by mask      ; rescale to 0..255
cmp/csel twice                 ; -> max(r, max(g, b))
strb                           ; one byte per LED
```

`screend` documents the same contract: *"Driver computes brightness =
max(R, G, B) for each LED"*, and it fills all three channels with the same
value.

### Brightness

`SET_BRIGHTNESS` = `1b <value>` + CRC (4 bytes) in both firmware families:
`sa_set_brightness` (`.text+0x2ee8`, `len = 2`) and
`set_brightness_app_cmd` (gowin `.text+0x4f98`, `len = 4`, 5 retries).
`max-brightness` from DT (200) becomes `backlight_properties.max_brightness`.

### Reset

`devm_sa_device_init` (`.text+0x2810`):

```
2994: gpiod_direction_output(reset, 0)   ; assert (ACTIVE_LOW)
29a8: msleep(10)
29b4: gpiod_set_value(reset, 1)          ; release
29c0: msleep(2000)                       ; wait for the app firmware
```

`gowin_led_device` waits 300 ms instead (`docs/screen.md` §3.2). The driver
uses the conservative 2 s.

### API version table

`.rodata+0x308` holds 10 records of `struct sa_fw_impl { bool supported;
bool has_separate_fw_version; bool has_crc_support; struct sa_ops ops; }`:

| api | bytes | meaning |
|---|---|---|
| 0..4 | `00 00 00 00` | unsupported |
| 5 | `01 00 00 00` + `spi_write` | supported, no CRC |
| 6 | `01 00 00 00` + `spi_write` | supported, no CRC |
| 7 | `01 01 00 00` + `spi_write` | separate fw version |
| 8 | `00 00 00 00` | unsupported |
| 9 | `01 01 01 00` + `sa_write_v9` | CRC write path |

Corroboration: `lib/firmware/yandex-led-screen.bin.manifest` says
`min_api_version = 7`, `max_api_version = 9`, `fw_version = 15`.

## Driver behaviour

* `probe`: `spi_setup` (mode 0, 8 bit, clock from `spi_hz`), optional reset
  pulse, protocol detect, geometry, fbdev + backlight registration.
* Framebuffer: 25x16, 24 bpp RGB888 (red@16, green@8, blue@0),
  `line_length = 75`, `screen_size = smem_len = ALIGN(1200, 4096) = 4096`
  (the vendor does the same rounding, and page alignment is needed for mmap).
* Update path: *not* `fb_deferred_io`. Deferred IO only tracks page faults of
  an mmap; `screend` uses `write()` + `close()`, which never touches those
  hooks. Instead every `fb_write()`, `fb_pan_display()` and `fb_release()`
  schedules a delayed work item (`refresh_ms`, default 20 ms) that converts the
  buffer, `memcmp()`s it against the last frame sent and pushes only when it
  changed. That covers `write()+close()`, long-lived `write()` fds and
  `mmap` + `FBIOPAN_DISPLAY` clients.
* Backlight: `/sys/class/backlight/<dt-node-name>/brightness`, range
  0..`max-brightness` (DT, 200). `update_status` sends the `SET_BRIGHTNESS`
  command; the frame is not resent (the FPGA keeps the level).
* teardown: the worker is stopped before devres frees the transfer buffers
  (also on unbind with an open fd, via the `removed` flag), `screen_buffer` is
  freed from `fb_destroy()` once the last user is gone.

Module parameters:

| param | default | purpose |
|---|---|---|
| `proto` | -1 | 0 = gowin (PA), 1 = quasar (SA), -1 = autodetect |
| `spi_hz` | 1000000 | clock; quasar clocks every transfer at 1 MHz although DT allows 4 |
| `refresh_ms` | 20 | coalescing delay before a push |
| `frame_format` | 0 | 0 = 8 bpp luminance, 1 = raw RGB888 (experimental) |
| `check_status` | 1 | read back the status word after each command |

## Assumptions (not proven from the binaries)

1. **Which firmware is on the panel.** Both vendor modules exist; only
   `gowin_led_device` binds with the current DT (compatible
   `"ya,led_screen"`). Autodetect tries PA first (a 35-byte idle read), then
   the SA poll. A 35-byte idle read against an SA panel is a malformed
   command; it is expected to be NAKed and makes the first SA status look
   stale, which only costs one resend.
2. **Geometry for PA.** The response has no xres/yres, so the driver takes
   `width`/`height` from DT and falls back to 25x16. The vendor uses a
   `board_rev` table (`rev2res_screen[1] = 25x16`).
3. **Reset timing** (10 ms low, 2 s wait) comes from `quasar_led_screen`;
   gowin only waits 300 ms. A cold panel that boots slower than 2 s will fail
   the first detect (the driver does not retry with another reset).
4. **`spi_hz` = 1 MHz.** `quasar_led_screen` sets `transfer.speed_hz = 1 MHz`
   on every transfer even though DT allows 4 MHz; `gowin_led_device` uses the
   DT value. 1 MHz is the conservative choice for bring-up.
5. **`frame_format = 1`** (raw RGB888) is a guess for a hypothetical firmware
   that does the reduction itself; nothing in the binaries suggests it exists.
6. **`next_frame_delay` is always 0.** The vendor exposes it via sysfs
   (`frame_queue/next_frame_delay`) for FPGA-side frame pacing; the driver
   does not queue frames, so there is nothing to pace. The field is still
   encoded (both firmwares expect it).
7. **fb numbering.** `screend` opens `/dev/fb4` literally. fb numbers follow
   probe order; pin them with a DT `aliases` entry (owned by the DTS work) or
   a udev rule. `fix.id` is `atrios-ledpanel`, not the vendor
   `"YNDX LED screen"`.

## Not implemented

* FPGA bitstream upload / JTAG (`gowin_led_device` part two): needs the
  `jtag_*` GPIOs *and* a pinctrl handoff for the pins shared with `spicc1`,
  plus `local_irq_save()` around the bit-banging. Only needed to reflash the
  panel; `docs/screen.md` §6 has the full sequence.
* Application firmware update over SPI (`bl_*` bootloader protocol in
  `quasar_led_screen`), file `lib/firmware/yandex-led-screen.bin`.
* hwmon `temp_celsius`, speaker/touch/UART/gpio-expander functions of the
  vendor driver, and all vendor sysfs attributes.

## Building the patch in

```sh
# in an unpacked Linux tree
scripts/apply.sh --kernel-only          # copies drivers/ and applies 0005
# or by hand
git apply patches/linux/0005-atrios-ledpanel.patch
```

`0005` carries the driver *and* the Kconfig/Makefile entries, so it is
self-contained; `scripts/apply.sh` copies `drivers/**` on top
afterwards, which is harmless.

Still needed outside this patch (other people's files, not touched here):
add `CONFIG_FB_ATRIOS_LEDPANEL=y` to `configs/atrios.config`. The symbol
selects `FB_SYSMEM_HELPERS` and `FB_BACKLIGHT` itself.

## Verifying on hardware

```sh
dmesg | grep -i ledpanel
# expected: "gowin protocol, firmware version=.., 25x16"
#           "25x16 24bpp framebuffer 'atrios-ledpanel'"

ls /sys/class/graphics/                      # find the new fbN and check name
cat /sys/class/graphics/fb4/name             # atrios-ledpanel
ls /sys/class/backlight/                     # one entry, max_brightness 200

# fill the panel with a solid grey frame (all three channels equal)
python3 -c 'open("/dev/fb4","wb").write(bytes([200,200,200])*400)'
# expect all 400 LEDs on; then write zeros to clear it
python3 -c 'open("/dev/fb4","wb").write(bytes(1200))'

# brightness
echo 50 > /sys/class/backlight/*/brightness
cat /sys/class/backlight/*/brightness

# force a backend if autodetect picks the wrong one
modprobe atrios-ledpanel proto=1
```

Logic analyser on `CS`/`CLK`/`MOSI` (and `MISO` for the status read), SPI mode
0, 1 MHz:

| event | MOSI | MISO |
|---|---|---|
| frame (PA) | 406 B: `07 00 <delay le> <400 B> <crc be>` | — |
| frame (SA) | 405 B: `07 <400 B> <delay le> <crc be>` | — |
| status (PA) | 35 B idle (all zero) | 35 B response, byte 1 = 1 |
| status (SA) | 22 B: `00 * 20` + CRC | 20 B response |
| brightness | 4 B: `1b <value> <crc be>` | — |

The CRC is CRC-16/XMODEM over the preceding bytes, MSB first; verify with
`python3 -c 'import crcmod'`-free code or `crc_itu_t()` in a scratch module.

Note that `check_status=1` means every frame is followed by a status read, so
a full `screend` update is two transfers (that is exactly what both vendor
drivers do).

## Known risks

1. **Protocol mix-up is the main failure mode**: a wrong backend yields a
   `/dev/fb4` that accepts writes and a panel that stays dark. `dmesg` shows
   the detected protocol; `proto=` forces it.
2. **`/dev/fb4` vs `/dev/fbN`** (see above) — `screend` fails silently
   (`except: pass`) if the number moved.
3. **Payload orientation** (row order, mirroring) is unverified on hardware;
   `screend` assumes top-down, left-to-right.
4. **First frame after probe** may be rejected once because autodetection
   leaves a stale `last_cmd_status`; the next write resends.
5. **No JTAG**: if the FPGA has no bitstream after a cold boot on mainline
   (unknown who loads it), the panel stays dark and no driver change helps.
6. Reflashing the panel (JTAG or the `bl_*` protocol) is **not** supported by
   this driver; a wrong sequence there can brick the panel until it is
   reflashed externally.
