# Поддерживаемые драйверы AtriOS Nebula

Статус драйверов ядра (`drivers/atrios/` и mainline-подсистем Linux 7.x) по поддерживаемым устройствам.

---

## Яндекс Станция Макс 53 (`station-max` / `yndxsttion53` / AtriStation Max)

Профили сборки: `config/boards/station-max.conf` и `config/boards/yndxsttion53.conf` (`meson-sm1-yandex-station-max.dts`, Amlogic S905X3 / SM1).

### Работают

| Драйвер / Модуль | Подсистема | Интерфейс / Устройство | Статус |
|---|---|---|---|
| `gowin_led_device.ko` | LED-матрица FPGA Gowin GW1N-9 | SPI + GPIO / MMIO | Работает |
| `quasar_led_screen.ko` | Кадровый буфер LED-матрицы 25×16 | `/dev/fb1` (`/dev/fb_screen`) | Работает |
| `leds-is31fl32xx.ko` | LED-контроллер IS31FL3236A | I2C | Работает |
| `quasar_led_ring.ko` | Верхнее RGB LED-кольцо (24 светодиода) | `/dev/fb2` (`/dev/fb_ring`) | Работает |
| `quasar_pcba_ids.ko` | Идентификаторы плат из EEPROM (`24c02`) | I2C (`pcba-ids`) | Работает |
| `rotary-volume.ko` | Энкодер громкости | GPIO interrupts (`/dev/input/event*`) | Работает |
| `gpio_keypad.ko` | Аппаратные кнопки управления | Polled GPIO (`/dev/input/event*`) | Работает |
| `zigbee-control.ko` | Сброс и bootloader Zigbee EFR32 | GPIO (`/sys/devices/platform/zigbee-control`, `/dev/ttyAML2`) | Работает |
| `ltr308als01.ko` | Датчик внешней освещённости | I2C / IIO (`iio:device0`, `0-0053`) | Работает |
| `snd-soc-sy6045s.ko` | Усилители НЧ/ВЧ с DSP biquad-фильтрами | I2C (`2-002a`, `2-002b`) / I2S | Работает |
| `snd-soc-es8156.ko` | Аудио ЦАП линейного выхода / наушников | I2C (`2-0008`) / I2S | Работает |
| `snd-soc-es7210.ko` | 4-канальный АЦП микрофонного массива | I2C (`2-0040`) / TDM | Работает |
| `snd-soc-dmic.ko` | PDM-массив микрофонов | Mainline ASoC (`AML-AUGESOUND`) | Работает |
| `axg-sound-card` | Звуковая карта Amlogic AXG | `/proc/asound/cards` (`card 0`) | Работает |
| `rtw88_8822cs` (`rtw88_core`, `rtw88_8822c`) | Wi-Fi Realtek RTL8822CS | SDIO (`wlan0`) | Работает |
| `hci_uart` / `btrtl` | Bluetooth 5.0 Realtek RTL8822CS | UART Serdev (`/dev/ttyAML1`, `hci0`) | Работает |
| `meson-drm` | HDMI-выход / основной фреймбуфер | `/dev/fb0` (`mesondrmfb`) | Работает |

### Не работают / не используются на этой ревизии

| Драйвер / Модуль | Причина | Статус |
|---|---|---|
| `snd-soc-tas5825m.ko` | Усилитель TAS5825M не установлен на данной плате (в DTS используются `sy6045s` на `2-002a`/`2-002b`; модуль загружается с `Used by 0`) | Не используется |
| `snd-soc-pcm1774` | ЦАП PCM1774 не установлен на данной плате (в DTS используется `es8156` на `2-0008`) | Не используется |
