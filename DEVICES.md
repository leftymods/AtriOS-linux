# Поддерживаемые устройства AtriOS Nebula (`config/boards/`)

| Board ID | Платформа | Семейство / SoC | Конфиг сборки | Статус |
|---|---|---|---|---|
| `station-max` (`yndxsttion53`) | **AtriStation Max** (Яндекс Станция Макс 53) | `amlogic-meson64` / Amlogic S905X3 (`sm1`) | `config/boards/station-max.conf` (`yndxsttion53.conf`) | Полная поддержка |
| `template` | Шаблон для новой колонки (`new-board`) | Настраивается (`amlogic-meson64` и др.) | `config/boards/template.conf` | Шаблон расширения |

---

## Добавление новой колонки во фреймворк

1. Скопируйте `config/boards/template.conf` в `config/boards/<board>.conf` и задайте параметры `BOARD`, `VENDOR`, `BOARDFAMILY`, `SOC`, `DTS_NAME`, `UBOOT_DEFCONFIG`, `KERNEL_DEFCONFIG`, `ROOTFS_OVERLAY`.
2. Разместите Device Tree, defconfig и overlay в `board/<vendor>/<device>/`.
3. Добавьте профиль платы (по `compatible` из Device Tree) в `/usr/lib/atrios/device-detect` (для `fastfetch`, `S00resizefs` и проверки совместимости при OTA).

Статус поддерживаемых драйверов ядра по устройствам см. в [DRIVERS.md](DRIVERS.md).
