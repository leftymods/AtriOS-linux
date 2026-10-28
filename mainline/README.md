# AtriOS mainline port

Goal: run the Yandex Station Max (Amlogic S905X3 / SM1) on **upstream**
software instead of the vendor Android 9 / kernel 4.9.113 stack.

| Component | Vendor baseline | Mainline target |
|---|---|---|
| Bootloader | U-Boot 2015.01 (Amlogic FIP) | U-Boot v2026.10 |
| Kernel | Linux 4.9.113 (Amlogic BSP) | Linux 7.3 |
| Userspace | Buildroot (see `main` branch) | Buildroot, reused |

Nothing in this directory is wired into the vendor build. `main`, `master`
and `slim` stay on the 4.9 stack; this branch is the upstream port only.

## Layout

```
mainline/
  docs/                 reverse-engineering notes and porting plan
  dts/                  board DTS for the mainline kernel
  drivers/              new drivers that do not exist upstream yet
  patches/              quilt-style patches against Linux 7.3 / U-Boot
  u-boot/               defconfig + DTS for mainline U-Boot
  scripts/              fetch, apply and build helpers
```

## Status

Porting order (agreed): everything else first, Wi-Fi last.

| Block | Vendor driver | Mainline plan | State |
|---|---|---|---|
| LED ring (24 RGB) | `leds-is31fl32xx` + `quasar_led_ring` | upstream `leds-is31fl32xx` + `ledtrig-ringfb` | in progress |
| Front LED matrix 25x16 | `gowin_led_device` + `quasar_led_screen` | new SPI driver | planned |
| Rotary volume | `rotary-volume` | port to input subsystem | planned |
| Ambient light | `ltr308als01` | upstream `ltrf216a` alias | in progress |
| PCBA ids | `quasar_pcba_ids` | nvmem/at24 + userspace | planned |
| Zigbee | `zigbee-control` | new serdev driver | planned |
| Audio | AML auge + es7210/es8156/sy6045s | ASoC meson + codecs | planned |
| Bluetooth | `88x2es`? no, `bt-dev` + H5 | `btrtl` + `hci_h5` | planned |
| Wi-Fi | `88x2es` (RTL8822ES) | rtw88 backend | **last** |
