#include <stdlib.h>

#define STUB_FN(name) void name() { abort(); }

STUB_FN(linkplay_bluetooth_cancel_scan);
STUB_FN(linkplay_bluetooth_close_all);
STUB_FN(linkplay_bluetooth_connect_by_addr);
STUB_FN(linkplay_bluetooth_event_dump);
STUB_FN(linkplay_bluetooth_get_connection_index_by_addr);
STUB_FN(linkplay_bluetooth_get_element);
STUB_FN(linkplay_bluetooth_get_remote_device_by_addr);
STUB_FN(linkplay_bluetooth_playback_next);
STUB_FN(linkplay_bluetooth_playback_pause);
STUB_FN(linkplay_bluetooth_playback_prev);
STUB_FN(linkplay_bluetooth_playback_resume);
STUB_FN(linkplay_bluetooth_play_stop_all);
STUB_FN(linkplay_bluetooth_registerAudioRenderCallBack);
STUB_FN(linkplay_bluetooth_registerEventCallbacks);
STUB_FN(linkplay_bluetooth_schedule_event);
STUB_FN(linkplay_bluetooth_set_abs_volume);
STUB_FN(linkplay_bluetooth_set_debug_level);
STUB_FN(linkplay_bluetooth_set_visibility);
STUB_FN(linkplay_bluetooth_source_send_stream);
STUB_FN(linkplay_bluetooth_source_start_stream);
STUB_FN(linkplay_bluetooth_start_scan);
STUB_FN(linkplay_bluetooth_start_services);
STUB_FN(linkplay_bluetooth_stop_stream);
STUB_FN(linkplay_bt_server_init);
