#include "linkplay_a98_bluetooth_impl.h"

using namespace quasar;

std::string LinkplayA98BluetoothImpl::btErrorStr(BtError btError)
{
    switch (btError) {
        case kBtErrorOk:
            return "kBtErrorOk";
        case kBtErrorFailed:
            return "kBtErrorFailed";
        case kBtErrorInitFailed:
            return "kBtErrorInitFailed";
        case KBtErrorInitProcessing:
            return "KBtErrorInitProcessing";
        case KBtErrorParamsInvaild:
            return "KBtErrorParamsInvalid";
        case kBtDevicesNoHistory:
            return "kBtDevicesNoHistory";
        case KBtAlreadyConnecting:
            return "KBtAlreadyConnecting";
        case kBtHistoryNotFound:
            return "kBtHistoryNotFound";
        case kBtConnectFailed:
            return "kBtConnectFailed";
        default:
            break;
    }
    return "UNDEFINED BT ERROR";
}

std::string LinkplayA98BluetoothImpl::btSecEvtStr(tBT_SEC_EVT btSecEvt)
{
    switch (btSecEvt) {
        case BT_SEC_LINK_UP_EVT:
            return "BT_SEC_LINK_UP_EVT";
        case BT_SEC_LINK_DOWN_EVT:
            return "BT_SEC_LINK_DOWN_EVT";
        case BT_SEC_AUTH_CMPL_EVT:
            return "BT_SEC_AUTH_CMPL_EVT";
        case BT_SEC_AUTHORIZE_EVT:
            return "BT_SEC_AUTHORIZE_EVT";
        case BT_SEC_RSSI_EVT:
            return "BT_SEC_RSSI_EVT";
        default:
            break;
    }
    return "UNDEFINED BT_SEC_EVT";
}

std::string LinkplayA98BluetoothImpl::btServEvtStr(tBT_SERV_EVT btServEvt)
{
    switch (btServEvt) {
        case BT_BSA_SERVER_INIT_SUCCESSFUL:
            return "BT_BSA_SERVER_INIT_SUCCESSFUL";
        case BT_BSA_SERVER_INIT_FAILED:
            return "BT_BSA_SERVER_INIT_FAILED";
        case BT_DISC_NEW_EVT:
            return "BT_DISC_NEW_EVT";
        case BT_DISC_CMPL_EVT:
            return "BT_DISC_CMPL_EVT";
        default:
            break;
    }
    return "UNDEFINED BT_SERV_EVT";
}

std::string LinkplayA98BluetoothImpl::btSinkEvtStr(tBT_SINK_EVT btSinkEvt)
{
    switch (btSinkEvt) {
        case BT_SINK_CONNECTED_EVT:
            return "BT_SINK_CONNECTED_EVT";
        case BT_SINK_DISCONNECTED_EVT:
            return "BT_SINK_DISCONNECTED_EVT";
        case BT_SINK_STREAM_START_EVT:
            return "BT_SINK_STREAM_START_EVT";
        case BT_SINK_STREAM_STOP_EVT:
            return "BT_SINK_STREAM_STOP_EVT";
        case BT_SINK_AVRCP_OPEN_EVT:
            return "BT_SINK_AVRCP_OPEN_EVT";
        case BT_SINK_AVRCP_CLOSE_EVT:
            return "BT_SINK_AVRCP_CLOSE_EVT";
        case BT_SINK_SET_ABS_VOL_CMD_EVT:
            return "BT_SINK_SET_ABS_VOL_CMD_EVT";
        case BT_SINK_GET_ELEM_ATTR_EVT:
            return "BT_SINK_GET_ELEM_ATTR_EVT";
        case BT_SINK_REMOTE_RC_TRACK_CHANGE:
            return "BT_SINK_REMOTE_RC_TRACK_CHANGE";
        case BT_SINK_REMOTE_RC_PLAY_EVT:
            return "BT_SINK_REMOTE_RC_PLAY_EVT";
        case BT_SINK_REMOTE_RC_PAUSE_EVT:
            return "BT_SINK_REMOTE_RC_PAUSE_EVT";
        case BT_SINK_REMOTE_RC_STOP_EVT:
            return "BT_SINK_REMOTE_RC_STOP_EVT";
        default:
            break;
    }
    return "UNDEFINED BT_SINK_EVT";
}

std::string LinkplayA98BluetoothImpl::btSourceEvtStr(tBT_SOURCE_EVT btSourceEvt)
{
    switch (btSourceEvt) {
        case BT_SOURCE_CONNECTED_EVT:
            return "BT_SOURCE_CONNECTED_EVT";
        case BT_SOURCE_DISCONNECTED_EVT:
            return "BT_SOURCE_DISCONNECTED_EVT";
        case BT_SOURCE_AV_START_EVT:
            return "BT_SOURCE_AV_START_EVT";
        case BT_SOURCE_AV_STOP_EVT:
            return "BT_SOURCE_AV_STOP_EVT";
        case BT_SOURCE_REMOTE_CMD_EVT:
            return "BT_SOURCE_REMOTE_CMD_EVT";
        case BT_SOURCE_CFG_RSP_EVT:
            return "BT_SOURCE_CFG_RSP_EVT";
        default:
            break;
    }
    return "UNDEFINED BT_SOURCE_EVT";
}

std::string LinkplayA98BluetoothImpl::btRoleStr(int btRole)
{
    switch (btRole) {
        case BT_ROLE_SINK:
            return "BT_ROLE_SINK";
        case BT_ROLE_SOURCE:
            return "BT_ROLE_SOURCE";
        default:
            break;
    }
    return "UNDEFINED ROLE";
}
