<div align="center">

# AtriOS

**Фреймворк для сборки Linux-прошивок умных колонок**

Профили устройств, ядро Linux и U-Boot, драйверы оборудования, системные службы и OTA.

[![Signed OTA](https://github.com/leftymods/AtriOS-linux/actions/workflows/release-ota-runtime.yml/badge.svg)](https://github.com/leftymods/AtriOS-linux/actions/workflows/release-ota-runtime.yml)

[Устройства](DEVICES.md) · [Драйверы](DRIVERS.md) · [Релизы](https://github.com/leftymods/AtriOS-linux/releases) · [История изменений](CHANGELOG.md)

</div>

---

## О проекте

AtriOS — фреймворк для разработки и сборки Linux-систем под колонки. Профиль платы описывает SoC, ядро, загрузчик, Device Tree, драйверы и rootfs; общие инструменты собирают пакеты и образы, а CI проверяет релизные артефакты.

Новые платы добавляются отдельными профилями. Сборки ядра и загрузчика изолированы по устройствам, а драйверы и системные службы подключаются к общей системе сборки.

## Устройства

Список плат, поддерживаемых моделей и статус поддержки находится в [DEVICES.md](DEVICES.md).

## Драйверы

Матрица драйверов, интерфейсов и статусов по устройствам находится в [DRIVERS.md](DRIVERS.md).

## Возможности фреймворка

- **Профили плат** — параметры SoC, DTS, ядра, загрузчика, rootfs и serial console.
- **Сборка** — ядро, U-Boot, пакеты, rootfs и готовые образы.
- **Изоляция** — отдельные профили и кэши ядра для разных плат; фиксированные версии исходников загрузчика.
- **Системная интеграция** — rootfs overlays, драйверы и службы на C и Rust.
- **Обновления** — подписанные OTA-артефакты, проверка целостности и поддержка отката.
- **Проверки** — тесты и CI для профилей, сценариев сборки и релизов.

## Быстрый старт

Команды выполняются из корня репозитория в Linux или WSL.

```bash
# Доступные профили
./compile.sh list

# Параметры профиля AtriStation
./compile.sh info BOARD=station-max

# Собрать полный образ
./compile.sh BOARD=station-max RELEASE=Nebula TARGET=image
```

Для отдельных этапов используйте `TARGET=kernel`, `TARGET=uboot`, `TARGET=rootfs` или `TARGET=packages`.

Проверки репозитория:

```bash
make test
make verify
```

## Структура репозитория

| Каталог | Назначение |
|---|---|
| `config/boards/` | Профили и параметры устройств |
| `config/bootloaders/` | Рецепты сборки загрузчиков |
| `board/` | Device Tree, конфигурации ядра, драйверы и rootfs overlays |
| `lib/`, `scripts/` | Сборка ядра, rootfs, пакетов и образов |
| `tests/` | Регрессионные и профильные тесты |
| `.github/workflows/` | CI и публикация прошивок и OTA |

## Документация

- [Устройства и добавление профиля платы](DEVICES.md)
- [Драйверы и статусы поддержки](DRIVERS.md)
- [История версий](CHANGELOG.md)
- [Готовые релизы](https://github.com/leftymods/AtriOS-linux/releases)
