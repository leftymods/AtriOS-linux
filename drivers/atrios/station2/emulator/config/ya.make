OWNER(g:yandex_io)

UNION()

RUN_PROGRAM(
    yandex_io/tools/config_tool
    quasar-dev.cfg
    ${ARCADIA_ROOT}/yandex_io/config/common.cfg
    ${ARCADIA_ROOT}/smart_devices/platforms/android_common/config/android_common.cfg
    ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/config/common.cfg
    ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/config/dev_overlay.cfg
    ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/emulator/config/emulator_overlay.cfg
    IN
        ${ARCADIA_ROOT}/yandex_io/config/common.cfg
        ${ARCADIA_ROOT}/smart_devices/platforms/android_common/config/android_common.cfg
        ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/config/common.cfg
        ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/config/dev_overlay.cfg
        ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/emulator/config/emulator_overlay.cfg
    OUT quasar-dev.cfg)

RUN_PROGRAM(
    yandex_io/tools/config_tool
    quasar-prod.cfg
    ${ARCADIA_ROOT}/yandex_io/config/common.cfg
    ${ARCADIA_ROOT}/smart_devices/platforms/android_common/config/android_common.cfg
    ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/config/common.cfg
    ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/emulator/config/emulator_overlay.cfg
    IN
        ${ARCADIA_ROOT}/yandex_io/config/common.cfg
        ${ARCADIA_ROOT}/smart_devices/platforms/android_common/config/android_common.cfg
        ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/config/common.cfg
        ${ARCADIA_ROOT}/smart_devices/platforms/yandexstation_2/emulator/config/emulator_overlay.cfg
    OUT quasar-prod.cfg)

END()
