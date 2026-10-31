OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zigbee/bdb/includes
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zigbee/common/includes
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zigbee/gp
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zigbee/ota
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zigbee/wwah
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zigbee/zbapi
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zigbee/zcl
)

PEERDIR(
    smart_devices/third_party/tl_zigbee_sdk/proj
)

SRCS(
    af/zb_af.c

    aps/aps_group.c

    bdb/bdb.c

    common/zb_config.c

    gp/gp.c
    gp/gp_proxyTab.c
    gp/gpEpCfg.c

    mac/mac_phy.c
    mac/mac_pib.c

    ota/ota.c
    ota/otaCRC.c
    ota/otaEpCfg.c

    ss/ss_nv.c

    wwah/wwah.c
    wwah/wwahEpCfg.c

    zcl/zcl.c
    zcl/zcl_nv.c
    zcl/zcl_reporting.c

    zcl/closures/zcl_door_lock.c
    zcl/closures/zcl_door_lock_attr.c
    zcl/closures/zcl_window_covering.c
    zcl/closures/zcl_window_covering_attr.c

    zcl/commissioning/zcl_commissioning.c
    zcl/commissioning/zcl_commissioning_attr.c

    zcl/general/zcl_alarm.c
    zcl/general/zcl_alarm_attr.c
    zcl/general/zcl_basic.c
    zcl/general/zcl_basic_attr.c
    zcl/general/zcl_binary_input.c
    zcl/general/zcl_binary_input_attr.c
    zcl/general/zcl_binary_output.c
    zcl/general/zcl_binary_output_attr.c
    zcl/general/zcl_devTemperatureCfg.c
    zcl/general/zcl_devTemperatureCfg_attr.c
    zcl/general/zcl_diagnostics.c
    zcl/general/zcl_diagnostics_attr.c
    zcl/general/zcl_greenPower.c
    zcl/general/zcl_greenPower_attr.c
    zcl/general/zcl_group.c
    zcl/general/zcl_group_attr.c
    zcl/general/zcl_identify.c
    zcl/general/zcl_identify_attr.c
    zcl/general/zcl_level.c
    zcl/general/zcl_level_attr.c
    zcl/general/zcl_multistate_input.c
    zcl/general/zcl_multistate_input_attr.c
    zcl/general/zcl_multistate_output.c
    zcl/general/zcl_multistate_output_attr.c
    zcl/general/zcl_onoff.c
    zcl/general/zcl_onoff_attr.c
    zcl/general/zcl_pollCtrl.c
    zcl/general/zcl_pollCtrl_attr.c
    zcl/general/zcl_powerCfg.c
    zcl/general/zcl_powerCfg_attr.c
    zcl/general/zcl_scene.c
    zcl/general/zcl_scene_attr.c
    zcl/general/zcl_time.c
    zcl/general/zcl_time_attr.c

    zcl/hvac/zcl_thermostat.c

    zcl/light_color_control/zcl_light_colorCtrl.c
    zcl/light_color_control/zcl_light_colorCtrl_attr.c

    zcl/measument_sensing/zcl_electrical_measurement.c
    zcl/measument_sensing/zcl_electrical_measurement_attr.c
    zcl/measument_sensing/zcl_illuminance_measurement.c
    zcl/measument_sensing/zcl_illuminance_measurement_attr.c
    zcl/measument_sensing/zcl_occupancy_sensing.c
    zcl/measument_sensing/zcl_occupancy_sensing_attr.c
    zcl/measument_sensing/zcl_temperature_measurement.c
    zcl/measument_sensing/zcl_temperature_measurement_attr.c

    zcl/ota_upgrading/zcl_ota.c
    zcl/ota_upgrading/zcl_ota_attr.c

    zcl/security_safety/zcl_ias_ace.c
    zcl/security_safety/zcl_ias_wd.c
    zcl/security_safety/zcl_ias_wd_attr.c
    zcl/security_safety/zcl_ias_zone.c
    zcl/security_safety/zcl_ias_zone_attr.c

    zcl/smart_energy/zcl_metering.c
    zcl/smart_energy/zcl_metering_attr.c

    zcl/zcl_wwah/zcl_wwah.c
    zcl/zcl_wwah/zcl_wwah_attr.c

    zcl/zll_commissioning/zcl_toucklink_security.c
    zcl/zll_commissioning/zcl_zll_commissioning.c
    zcl/zll_commissioning/zcl_zllTouchLinkDiscovery.c
    zcl/zll_commissioning/zcl_zllTouchLinkJoinOrStart.c

    zdo/zdp.c
)

IF (TL_ZB_ROLE == "coordinator")
    SRCS(
        lib/riscv/libzb_coordinator.a
    )
ELSEIF (TL_ZB_ROLE == "router")
    SRCS(
        lib/riscv/libzb_router.a
    )
ELSEIF (TL_ZB_ROLE == "end_device")
    SRCS(
        lib/riscv/libzb_ed.a
    )
ENDIF()

END()
