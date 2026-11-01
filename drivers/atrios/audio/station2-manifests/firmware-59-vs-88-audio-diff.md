# Прошивка 59 vs 88: что изменилось в аудио

Сравнение `firmware/yandexstation_2/{59,88}/manifest.xml` по аудиорепозиториям.

| Репозиторий | 59 | 88 | Изменение |
|---|---|---|---|
| `yandex-io/kernel/drivers/tas5825m` | `008e1eb0` | `e6f38533` | **изменён** |
| `yandex-io/kernel/drivers/sy6045s` | `44550212` | `92cccf17` | **изменён** |
| `yandex-io/kernel/drivers/es7210` | `d87f8e09` | `a9835172` | **изменён** |
| `yandex-io/kernel/drivers/es8156` | `07fb359a` | `07fb359a` | без изменений |
| `yandex-io/kernel/drivers/pcm1774` | `f3c55b4d` | `f3c55b4d` | без изменений |
| `yandex-io/kernel/drivers/tlv320adc3101` | `218fcfa3` | `218fcfa3` | без изменений |
| `yandex-io/kernel/drivers/rotary-volume` | `6e54bf2a` | `6e54bf2a` | без изменений |
| `unity/p-amlogic/platform/hardware/amlogic/audio` | `44e4416d` | `83824590` | **изменён** |
| `unity/platform/external/libaudioeffect` | `a9ebea75` | `a9ebea75` | без изменений |
| `unity/platform/hardware/amlogic/LibAudio` | `65596d92` | `65596d92` | без изменений |
| `unity/platform/packages/apps/SoundRecorder` | `8233a4f6` | `8233a4f6` | без изменений |

## Что это значит для TDM

TDM-режим (слоты, frame sync, routing) задаётся **не в манифесте**, а в:

1. **Device Tree** ядра (`unity/kernel/common`) — `tdm`/`i2s` узлы, `dai-link`,
   `mclk`, `bclk`, `lrclk`, число слотов;
2. **Amlogic audio HAL** (`hardware/amlogic/audio`) — маршрутизация и микшер;
3. **драйверах кодеков/усилителей** (`tas5825m`, `sy6045s`, `es7210`, `es8156`,
   `tlv320adc3101`, `pcm1774`) — формат данных, TDM/I²S, клоки.

В архивах **нет ни одного файла с `tdm`** (ни в `smart_devices`, ни в
`yandex_io`), и нет DTS/драйверов ядра — только записи манифестов. Поэтому
«вытащить TDM из 59» и «повторить в новых» из этих архивов нельзя.
Нужен доступ к внутреннему Gerrit и `repo sync` обоих наборов ревизий.
