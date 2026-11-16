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
4. Add a reviewed boot recipe at `config/bootloaders/<DTS_NAME>.conf` (or select its name through `BOOTLOADER_PROFILE`). The recipe must set repository-relative `UBOOT_CONFIG_SOURCE`, `UBOOT_DTS_SOURCE`, tree-relative `UBOOT_DTS_TARGET`, and an explicit board-compatible `FIP_PROFILE`. Optional `UBOOT_DTS_OVERLAY_SOURCE`/`UBOOT_DTS_OVERLAY_TARGET`, `UBOOT_AML_BURN_SOURCE`, and `UBOOT_REQUIRED_OPTIONS` belong to that recipe. Use the existing Station recipe as a schema example, not as firmware for another board. `UPGRADE_DIR` selects this board's output directory; a missing recipe fails before compilation instead of using Station firmware.
5. Register hardware approved for publication in both the release workflow matrix and its publish allowlist. CI resolves the trust anchor, DTB and image filename from that board's configuration. Adding a config or passing synthetic build tests does not establish physical hardware support.


Статус поддерживаемых драйверов ядра по устройствам см. в [DRIVERS.md](DRIVERS.md).
