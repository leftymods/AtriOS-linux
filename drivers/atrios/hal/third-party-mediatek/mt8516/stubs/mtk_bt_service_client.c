#include <stdlib.h>

#define STUB_FN(name) void name() { abort(); }

STUB_FN(a_mtkapi_a2dp_disconnect);
STUB_FN(a_mtkapi_a2dp_register_callback);
STUB_FN(a_mtkapi_a2dp_sink_enable);
STUB_FN(a_mtkapi_a2dp_sink_start_player);
STUB_FN(a_mtkapi_a2dp_sink_stop_player);
STUB_FN(a_mtkapi_a2dp_sound_mute);
STUB_FN(a_mtkapi_a2dp_sound_set_fade);
STUB_FN(a_mtkapi_avrcp_register_callback);
STUB_FN(a_mtkapi_avrcp_send_passthrough_cmd);
STUB_FN(a_mtkapi_avrcp_update_absolute_volume);
STUB_FN(a_mtkapi_bt_bluetooth_factory_reset);
STUB_FN(a_mtkapi_bt_gap_get_dev_info);
STUB_FN(a_mtkapi_bt_gap_on_off);
STUB_FN(a_mtkapi_bt_gap_set_connectable_and_discoverable);
STUB_FN(a_mtkapi_bt_gap_set_name);
STUB_FN(a_mtkapi_bt_gap_start_inquiry_scan);
STUB_FN(a_mtkapi_gap_bt_base_init);
STUB_FN(a_mtk_bt_service_init);
