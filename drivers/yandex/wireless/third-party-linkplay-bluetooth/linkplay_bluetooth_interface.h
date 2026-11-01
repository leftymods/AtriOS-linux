/**
 * \file linkplay_bluetooth_interface.h
 * \brief The public linkplay bluetooth API
 * \copyright Copyright 2018 Linkplay Inc. All rights reserved.
 */

#ifndef __LINKPLAY_BLUETOOTH_INTERFACE_H__
#define __LINKPLAY_BLUETOOTH_INTERFACE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned char BOOLEAN;

#define BD_ADDR_LEN 6
#define BD_NAME_LEN 248

typedef UINT8 BD_ADDR[BD_ADDR_LEN];
typedef UINT8 BD_NAME[BD_NAME_LEN + 1]; /* Device name */

/**
 * \brief The version of the API defined in this header file.
 */
#define LINKPLAY_BLUETOOTH_SDK_VERSION 10

typedef enum {
    /** \brief The operation was successful. */
    kBtErrorOk = 0,
    /** \brief The operation failed due to an unspecified issue. */
    kBtErrorFailed = 1,
    /**
     * \brief The bsa server could not be initialized.
     * \see linkplay_bt_server_init
     */
    kBtErrorInitFailed = 2,
    /**
     * \brief The bsa server is starting
     * \see linkplay_bt_server_init
     */
    KBtErrorInitProcessing = 3,
    /** \brief An unexpected argument value was passed to a function. */
    KBtErrorParamsInvaild = 4, /**
    * \brief Currently bluetooth try connecting
    * \see linkplay_bluetooth_connect_by_addr
    */
    KBtAlreadyConnecting = 6,
    /**
     * \brief  the remove device not found in bluetooth history "/vendor/bsa/bt_devices.xml"
     * \see linkplay_bluetooth_connect_by_addr
     */
    kBtDevicesNoHistory = 5,
    /**
     * \brief the file "/vendor/bsa/bt_devices.xml" not exsit
     * \see linkplay_bluetooth_connect_by_addr
     */
    kBtHistoryNotFound = 7,
    /**
     * \brief the remove devie has been connected failed
     * \see linkplay_bluetooth_connect_by_addr
     */
    kBtConnectFailed = 8,

} BtError;

typedef enum {
    BT_SEC_LINK_UP_EVT,   /* A device is physically connected (for info) */
    BT_SEC_LINK_DOWN_EVT, /* A device is physically disconnected (for info)*/
    BT_SEC_AUTH_CMPL_EVT, /* pairing/authentication complete */
    BT_SEC_AUTHORIZE_EVT, /* Authorization request */
    BT_SEC_RSSI_EVT,      /* RSSI report received */
} tBT_SEC_EVT;

typedef enum {
    BT_BSA_SERVER_INIT_SUCCESSFUL, /* bsa server inited successful */
    BT_BSA_SERVER_INIT_FAILED,     /* bsa server inited unsuccessful */
    BT_DISC_NEW_EVT,               /* a New Device has been discovered */
    BT_DISC_CMPL_EVT,              /* End of Discovery */
} tBT_SERV_EVT;

typedef enum {
    BT_SINK_CONNECTED_EVT,          /* the mobile bluetooth connected */
    BT_SINK_DISCONNECTED_EVT,       /* the mobile bluetooth disconnected */
    BT_SINK_STREAM_START_EVT,       /* stream data transfer started */
    BT_SINK_STREAM_STOP_EVT,        /* stream data transfer stopped */
    BT_SINK_AVRCP_OPEN_EVT,         /* remote control channel open */
    BT_SINK_AVRCP_CLOSE_EVT,        /* remote control channel close */
    BT_SINK_SET_ABS_VOL_CMD_EVT,    /* AVRCP abs vol command  */
    BT_SINK_GET_ELEM_ATTR_EVT,      /* get element attrubute response */
    BT_SINK_REMOTE_RC_TRACK_CHANGE, /* AVRCP track change  */
    BT_SINK_REMOTE_RC_PLAY_EVT,     /* AVRCP play command  */
    BT_SINK_REMOTE_RC_PAUSE_EVT,    /* AVRCP pause command  */
    BT_SINK_REMOTE_RC_STOP_EVT,     /* AVRCP stop command  */
} tBT_SINK_EVT;

typedef enum {
    BT_SOURCE_CONNECTED_EVT,    /* the external bluetooth speaker connected */
    BT_SOURCE_DISCONNECTED_EVT, /* the external bluetooth speaker disconnected */
    BT_SOURCE_AV_START_EVT,     /* stream data transfer started */
    BT_SOURCE_AV_STOP_EVT,      /* stream data transfer stopped */
    BT_SOURCE_REMOTE_CMD_EVT,   /* remote control command */
    BT_SOURCE_CFG_RSP_EVT,      /* stream open stream sample config  */
} tBT_SOURCE_EVT;

typedef enum {
    BT_HFP_AUDIO_OPEN_EVT,      /* Audio connection open */
    BT_HFP_AUDIO_CLOSE_EVT,     /* Audio connection closed */
    BT_HFP_CONNECTED_EVT,       /* connection established */
    BT_HFP_DISCONNECTED_EVT,    /* connection close */
    BT_HFP_RING_EVT,            /* ring alert from AG */
    BT_HFP_CLIP_EVT,            /* Calling subscriber information from AG */
    BT_HFP_CIND_EVT,            /* Indicator string from AG */
    BT_HFP_CIEV_EVT,            /* Indicator status from AG */
    BT_HFP_SPEAKER_VOL_SET_EVT, /* Speaker volume setting */
} tBT_HFP_EVT;

enum {
    BT_ROLE_SINK,   /* current role is a2dp sink mode */
    BT_ROLE_SOURCE, /* current role is a2dp source mode */
};

typedef struct {
    int msg_type;     // BLUETOOTH_SERVICES_EVENT BLUETOOTH_SEC_LINK_EVENT BLUETOOTH_SINK_EVENT
                      // BLUETOOTH_SOURCE_EVENT BLUETOOTH_HFP_EVENT
    int notify_event; // The subtype of the message type
    int notify_value;
    char *msg_value; // json
} bt_event_msg;

#define LINKPLAY_AVRC_PLAYSTATE_STOPPED 0x00 /* Stopped */
#define LINKPLAY_AVRC_PLAYSTATE_PLAYING 0x01 /* Playing */
#define LINKPLAY_AVRC_PLAYSTATE_PAUSED 0x02  /* Paused  */

/**
 * \brief This event occurs when bluetooth is initializing and scanning
 * \see bt_event_msg
 */

#define BLUETOOTH_SERVICES_EVENT 1
/**
 * \brief This event occurs when bluetooth is linking and authenticating
 * \see bt_event_msg
 */
#define BLUETOOTH_SEC_LINK_EVENT 2
/**
 * \brief This event occurs the a2dp sink protocol stack
 * \see bt_event_msg
 */
#define BLUETOOTH_SINK_EVENT 3
/**
 * \brief This event occurs the a2dp source protocol stack
 * \see bt_event_msg
 */
#define BLUETOOTH_SOURCE_EVENT 4

/**
 * \brief This event occurs the  HFP stack
 * \see bt_event_msg
 */
#define BLUETOOTH_HFP_EVENT 5

#define BT_A2DP_SINK (1 << 2)
#define BT_A2DP_SOURCE (1 << 3)
#define BT_HFP_HF (1 << 4)

/**
 * \brief call back for bsa server start,
  * bsa_server is the main bluetooth process,
  * and the libraries communicate with it.
 */
typedef void (*fn_bsa_server_init)(int enable);

typedef struct {
    UINT16 sampling_freq; /* 44100 only support */
    UINT16 num_channel;   /* 1 for mono or 2 stereo */
    UINT8 bit_per_sample; /* Number of bits per sample (8, 16) */
} tLINKPLAY_AV_MEDIA_FEED_CFG_PCM;

/**
 * \brief configure of bluetooth
 * \see linkplay_bt_server_init
 */
typedef struct {
    BD_ADDR bt_addr; /* bluetooth address set mac is not support , reserved for future */
    BD_NAME bt_name; /* bluetooth name */
    BOOLEAN support_aac_decode;
    fn_bsa_server_init bas_server_init_call_back;
} linkplay_bt_config;

typedef struct {
    BD_ADDR bt_addr; /* bluetooth address*/
    BD_NAME bt_name; /* bluetooth name*/
    UINT32 support_services;
} REMOTE_DEVICE_INFO;

/**
 * \brief Request a pcm device open for playing
 *
 * \param[in] context Context passed to linkplay_bluetooth_registerAudioRenderCallBack
 * \param[in] format snd_pcm_format_t
 * \param[in] sample_rate pcm sample rate ,should be 44100
 * \param[in] num_channel pcm channel ,should be 2
 * \param[in] bit_per_sample ,should be 16
  * \return Returns the pcm handle
 */

typedef void *(*fn_pcm_open)(void *context, UINT8 format, UINT16 sample_rate, UINT8 num_channel,
                             UINT8 bit_per_sample);
/**
 * \brief close pcm device
 *
 * \param[in] context Context passed to linkplay_bluetooth_registerAudioRenderCallBack
 */

typedef void (*fn_pcm_close)(void *context, void *pcm_handle);

/**
 * \brief  Callback for writing pcm data of AVDTP
 *
 * \param[in] context  passed to linkplay_bluetooth_registerAudioRenderCallBack
 * \param[in] connection index , default 0
 * \param[in] p_buffer pcm buffer
 * \param[in] length  pcm length
 * \ the return value is expected equal to frames
 */
typedef int (*fn_pcm_write_data)(void *context, void *pcm_handle, int connection, UINT8 *p_buffer,
                                 int length);

typedef struct {
    fn_pcm_open pcm_open_call_fn;
    fn_pcm_close pcm_close_call_fn;
    fn_pcm_write_data pcm_write_data_fn;
} pcm_render_call_back;

/**
 * \brief Callback for notifying the application about the bluetooth event
 *
 * To register this callback, use the function linkplay_bluetooth_registerEventCallbacks().
 *
 * \param[in] msg Structure with pointers to bluetooth event msg
 * \param[in] context Context pointer that was passed when registering the callback
 *
 * \note The application should not block or call API functions that are not allowed
 * in the callback.
 */

typedef int (*linkplay_bluetooth_event_callback)(bt_event_msg *msg, void *context);

/**
 * \brief Register callbacks related to bluetooth event
 *
 * \param[in] cb Structure with pointers to individual callback functions.
 *              Any of the pointers in the structure may be NULL.
 * \param[in] context Application-defined pointer that will be passed unchanged as
 *              the \a context argument to the callbacks.
 * \return Returns an error code
 */
BtError linkplay_bluetooth_registerEventCallbacks(linkplay_bluetooth_event_callback callback,
                                                  void *context);

/**
 * \brief Immediately interrupt the wait with linkplay_bluetooth_event_dump
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_schedule_event(void);

/**
 * \brief Waitting for the bluetooth library event,If the event is triggered, the event call will be
 * execute
 *
 * \param[in] timeoutms,The Max timeout of milliseconds for waitting valid event, if timeoutms equal
 * to -1, the timeout is infinite
 * \return Returns an error code
 */
BtError linkplay_bluetooth_event_dump(int timeoutms);

/**
 * \brief  Initialize the bluetooth server
 *
 * \param[in] bt_config Configuration parameters
 * \return Returns an error code
 */
BtError linkplay_bt_server_init(linkplay_bt_config *bt_config);

/** important  !!!!!!!!! sink must start before source
 * \brief  start the bluetooth special protocol stacks, such as a2dp
 *
 * \param[in] services BT_A2DP_SINK/BT_A2DP_SOURCE
 * \return Returns an error code
 * \ Note important !!!!!!!!! sink must start before source
 */
BtError linkplay_bluetooth_start_services(int services);

/**
 * \brief  stop the bluetooth special protocol stacks, such as a2dp
 *
 * \param[in] services BT_A2DP_SINK/BT_A2DP_SOURCE
 * \return Returns an error code
 */
BtError linkplay_bluetooth_stop_services(int services);

/**
* \brief  Set the Device Visibility parameters (InquiryScan & PageScan)
* \param[in] discoverable FALSE if not discoverable
* \param[in] connectable: FALSE if not connectable
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_set_visibility(BOOLEAN discoverable, BOOLEAN connectable);

/**
* \brief pause the stream play
* \param[in] role, BT_ROLE_SINK or BT_ROLE_SOURCE
* \param[in] index: connection index, default is 0
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_playback_pause(int role, int index);

/**
* \brief start the stream play
* \param[in] role, BT_ROLE_SINK or BT_ROLE_SOURCE
* \param[in] index: connection index, default is 0
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_playback_resume(int role, int index);

/**
* \brief  play next track
* \param[in] role, BT_ROLE_SINK
* \param[in] index: connection index, default is 0
** Returns  Returns an error code
* Note  this is only vaild in a2dp sink mode
*/
BtError linkplay_bluetooth_playback_next(int role, int index);

/**
* \brief  play prev track
* \param[in] role, BT_ROLE_SINK
* \param[in] index: connection index, default is 0
** Returns  Returns an error code
* Note  this is only vaild in a2dp sink mode
*/
BtError linkplay_bluetooth_playback_prev(int role, int index);

/**
* \brief stop stream plack
* \param[in] role, BT_ROLE_SINK or BT_ROLE_SOURCE
* \param[in] index: connection index, default is 0
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_play_stop(int role, int index);

/**
* \brief disconnect the remote device link
* \param[in] role, BT_ROLE_SINK or BT_ROLE_SOURCE
* \param[in] index: connection index, default is 0
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_close(int role, int index);

/**
* \brief  stop all connections stream plack
* \param[in] role, BT_ROLE_SINK or BT_ROLE_SOURCE
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_play_stop_all(int role);

/**
* \brief  disconnect all connections link
* \param[in] role, BT_ROLE_SINK or BT_ROLE_SOURCE
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_close_all(int role);

/**
* \brief  set abs vol AVRCP
* \param[in] role, BT_ROLE_SINK or BT_ROLE_SOURCE
* \param[in] volume 0 ~ 127
* \param[in] index: connection index, default is 0
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_set_abs_volume(int role, int volume, int index);

/**
* \brief  try to connect the remote device by bluetooth mac address, the function is block
* \param[in] role, BT_ROLE_SINK or BT_ROLE_SOURCE
* \param[in] mac remote devices mac address
* \param[in] timeoutms the maximum timeout millisecond waiting
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_connect_by_addr(int role, BD_ADDR *mac, int timeoutms);

/**
* \brief  Start Device discovery
* \param[in] duration,   Multiple of 1.28 seconds , total time is duration * 1.28s
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_start_scan(int duration);

/**
* \brief  Cancel Device discovery
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_cancel_scan(void);

/**
 * \brief Register callbacks related to pcm playing
 *
 * \param[in] cb Structure with pointers to individual callback functions.
 *              Any of the pointers in the structure may be NULL.
 * \param[in] context User context pointer that will be passed to fn_pcm_start and
 * fn_pcm_write_data.
 * \return Returns an error code
 */
BtError linkplay_bluetooth_registerAudioRenderCallBack(pcm_render_call_back callback,
                                                       void *context);

/**
 * \brief Start AV pcm transmission
 *
 * \param[in] pcm_config Structure with pointers to av media configure.
 * \return Returns an error code
 */
BtError linkplay_bluetooth_source_start_stream(tLINKPLAY_AV_MEDIA_FEED_CFG_PCM *pcm_config);

/**
 * \brief Write the transmitted PCM data, this function is multi-thread protect, can be call in
 * another thread
 *
 * \param[in] audio_buf , pointer to the PCM buffer
 * \param[in] length , the length of PCM data,in bytes
 * \return Returns an error code
 */
BtError linkplay_bluetooth_source_send_stream(void *audio_buf, int length);

/**
 * \brief Stop AV pcm transmission
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_stop_stream(void);

/**
 *  Get he connection index by remote mac address
 * \param[in] bd_addr, the remote devies mac address
 * \return Returns an error code
 * Note, Multiple connections is currently supported for sink mode only
 */
int linkplay_bluetooth_get_connection_index_by_addr(BD_ADDR bd_addr);

/**
 * \brief The paired devices information saved
 * \param[in] remote_device, point to remote device information
 * \param[in] bd_addr, the remote devies mac address
 * \return Returns an error code
 * Note, Multiple connections is currently supported for sink mode only
 */
BtError linkplay_bluetooth_get_remote_device_by_addr(REMOTE_DEVICE_INFO *remote_device,
                                                     BD_ADDR bt_addr);

/**
 * \brief Forget the remote device
 * \param[in] bd_addr, the remote devies mac address
 * \return Returns an error code
 */
int linkplay_bluetooth_unpair_remote_device(BD_ADDR *addr);

/**
 * \brief set the debug message level
 * \param[in] level,
#define DEBUG_LEVEL_0_ENABLE   (1 << 0)
#define DEBUG_LEVEL_1_ENABLE   (1 << 1)
#define DEBUG_LEVEL_2_ENABLE   (1 << 2)
#define DEBUG_LEVEL_3_ENABLE   (1 << 3)
 */
void linkplay_bluetooth_set_debug_level(unsigned int level);
/**
 * \brief get_element_attr_command
 * \param[in] index: connection index, default is 0
 * \ return Returns an error code
 */
BtError linkplay_bluetooth_get_element(int index);

/**
 * \brief send AVRCP event by source
 * \param[in] event,
#define LINKPLAY_AVRC_PLAYSTATE_STOPPED                  0x00
#define LINKPLAY_AVRC_PLAYSTATE_PLAYING                   0x01
#define LINKPLAY_AVRC_PLAYSTATE_PAUSED                    0x02
 */
void linkplay_bluetooth_av_rc_change(int event);

/**
 * \
 * \brief when  the audio open ,the call start and return 1
 *        when  the call end ,the auido close and retun 0
 * \
 */
int linkplay_bluetooth_is_calling();

/**
 * \brief  After  the audio open , answer the call.
 * \
 */
int linkplay_bluetooth_answer_call();

/**
 * \brief   End up  the call,  then  the audio close.
 * \
 */
int linkplay_bluetooth_hangup_call();

/**
 * \brief  raise a call with a phone number
 * \param[in] num, phone number
 */
int linkplay_bluetooth_dial_num(char *num);

/**
 * \brief  raise a call with the last dialed number
 * \
 */
int linkplay_bluetooth_dial_last_num(void);

/**
 * \brief  Make sure  the capture rate of   hfp pcm is 16000 or  8000  (times/sec)
 * \
 */
UINT32 linkplay_bluetooth_get_hfp_capture_rate();

/**
 * \brief  Enable the device that can tell the  connections its battery
 * \
 */
int linkplay_bluetooth_enable_battery_indicate();

/**
 * \brief
 * \  tell the  connections its battery
 *  \param[in] between 0 and 9
 */
int linkplay_bluetooth_set_battery_indicate(UINT32 level);

int linkplay_bluetooth_get_addr(char *addr);

/////BLE Interface
#ifndef GATT_MAX_ATTR_LEN
#define GATT_MAX_ATTR_LEN 600
#endif

#ifndef MAX_UUID_SIZE
#define MAX_UUID_SIZE 16
#endif

typedef struct {
#define LEN_UUID_16 2
#define LEN_UUID_32 4
#define LEN_UUID_128 16

    UINT16 len;

    union {
        UINT16 uuid16;
        UINT32 uuid32;
        UINT8 uuid128[MAX_UUID_SIZE];
    } uu;

} tBLE_UUID;

typedef struct ble_create_service_params {
    int server_num; /* server number which is distributed */
    int attr_num;
    tBLE_UUID service_uuid; /*service uuid */
    UINT16 num_handle;
    BOOLEAN is_primary;
} ble_create_service_params;

typedef struct bsa_ble_add_character_params {
    int server_num;
    int srvc_attr_num;
    int char_attr_num;
    int is_descript;
    int attribute_permission;
    int characteristic_property;
    tBLE_UUID char_uuid;
} ble_char_params;
typedef struct bsa_ble_server_indication {
    int server_num;
    int attr_num;
    int length_of_data;
    UINT8 *value;
    BOOLEAN need_confirm; /* TRUE: Indicate, FALSE: Notify */
} ble_server_indication;

typedef struct {
    UINT16 status;   /* operation status */
    UINT8 server_if; /* Server interface ID */
} tBLE_SE_DEREGISTER_MSG;

typedef struct {
    UINT8 server_if;
    UINT16 service_id;
    UINT16 status;
} tBLE_SE_CREATE_MSG;

typedef struct {
    UINT8 server_if;
    UINT16 service_id;
    UINT16 attr_id;
    UINT16 status;
    BOOLEAN is_discr;
} tBLE_SE_ADDCHAR_MSG;

typedef struct {
    UINT8 server_if;
    UINT16 service_id;
    UINT16 status;
} tBLE_SE_START_MSG;

typedef struct {
    BD_ADDR remote_bda;
    UINT32 trans_id;
    UINT16 conn_id;
    UINT16 handle;
    UINT16 offset;
    BOOLEAN is_long;
    UINT16 status;
} tBLE_SE_READ_MSG;

typedef struct {
    BD_ADDR remote_bda;
    UINT32 trans_id;
    UINT16 conn_id;
    UINT16 handle; /* attribute handle */
    UINT16 offset; /* attribute value offset, if no offset is needed for the command, ignore it */
    UINT16 len;    /* length of attribute value */
    UINT8 value[GATT_MAX_ATTR_LEN]; /* the actual attribute value */
    BOOLEAN need_rsp;               /* need write response */
    BOOLEAN is_prep;                /* is prepare write */
    UINT16 status;
} tBLE_SE_WRITE_MSG;

typedef struct {
    UINT8 server_if;
    BD_ADDR remote_bda;
    UINT16 conn_id;
    UINT16 reason;
} tBLE_SE_OPEN_MSG;

typedef tBLE_SE_OPEN_MSG tBLE_SE_CLOSE_MSG;

typedef struct {
    UINT16 conn_id; /* connection ID */
    UINT8 status;   /* notification/indication status */
} tBLE_SE_CONF_MSG;

typedef struct {
    UINT16 conn_id;
    BOOLEAN congested; /* congestion indicator */
} tBLE_SE_CONGEST_MSG;

typedef struct {
    UINT16 conn_id;
    UINT16 mtu;
} tBLE_SE_MTU_MSG;

typedef struct {
    void (*bt_gatts_server_create_cb)(void *context, tBLE_SE_CREATE_MSG *p_data);
    void (*bt_gatts_add_char_cb)(void *context, tBLE_SE_ADDCHAR_MSG *p_data);
    void (*bt_gatts_start_cb)(void *context, tBLE_SE_START_MSG *p_data);
    void (*bt_gatts_req_read_cb)(void *context, tBLE_SE_READ_MSG *p_data);
    void (*bt_gatts_req_write_cb)(void *context, tBLE_SE_WRITE_MSG *p_data);
    void (*bt_gatts_connection_open_cb)(void *context, tBLE_SE_OPEN_MSG *p_data);
    void (*bt_gatts_connection_close_cb)(void *context, tBLE_SE_CLOSE_MSG *p_data);
    void (*bt_gatts_set_mtu_cb)(void *context, tBLE_SE_MTU_MSG *p_data);
    void *context;
} tBT_APP_GATTS_CB_FUNC_T;

#ifndef BSA_DM_BLE_AD_DATA_LEN
#define BSA_DM_BLE_AD_DATA_LEN                                                                     \
    31 /*BLE Advertisement data size limit, stack takes 31bytes of data */
#endif

#ifndef BSA_DM_BLE_AD_UUID_MAX
#define BSA_DM_BLE_AD_UUID_MAX 6 /*Max number of Service UUID the device can advertise*/
#endif

typedef struct {
    UINT16 low;
    UINT16 hi;
} tBLE_INT_RANGE;

typedef struct {
    BOOLEAN list_cmpl;
    UINT8 uuid128[MAX_UUID_SIZE];
} tBLE_128SERVICE;

typedef struct {
    UINT8 adv_type;
    UINT8 len; /* number of len byte*/
    UINT8 *p_val;
} tBLE_PROP_ELEM;

/* vendor proprietary adv type */
typedef struct {
    UINT8 num_elem;
    tBLE_PROP_ELEM *p_elem;
} tBLE_PROPRIETARY;

/* BLE Advertisement configuration parameters */
typedef struct {
    UINT8 len;                           /* Number of bytes of data to be advertised */
    UINT8 flag;                          /* AD flag value to be set */
    UINT8 p_val[BSA_DM_BLE_AD_DATA_LEN]; /* Data to be advertised */
    UINT32 adv_data_mask;   /* Data Mask: Eg: BTM_BLE_AD_BIT_FLAGS, BTM_BLE_AD_BIT_MANU */
    UINT16 appearance_data; /* Device appearance data */
    UINT8 num_service;      /* number of services */
    UINT16 uuid_val[BSA_DM_BLE_AD_UUID_MAX];
    /* for DataType Service Data - 0x16 */
    UINT8 service_data_len;      /* length = AD type + service data uuid + data) */
    tBLE_UUID service_data_uuid; /* service data uuid */
    UINT8 service_data_val[BSA_DM_BLE_AD_DATA_LEN];
    BOOLEAN is_scan_rsp; /* is the data scan response or adv data */
    UINT8 tx_power;
    tBLE_INT_RANGE int_range;
    tBLE_128SERVICE services_128b;
    tBLE_128SERVICE sol_service_128b;
    tBLE_PROPRIETARY elem;
} tBLE_ADV_CONFIG;

#ifndef BSA_DM_BLE_ADDR_PUBLIC
#define BSA_DM_BLE_ADDR_PUBLIC 0x00
#endif

#ifndef BSA_DM_BLE_ADDR_RANDOM
#define BSA_DM_BLE_ADDR_RANDOM 0x01
#endif

#ifndef BSA_DM_BLE_ADDR_TYPE_MASK
#define BSA_DM_BLE_ADDR_TYPE_MASK (BSA_DM_BLE_ADDR_RANDOM | BSA_DM_BLE_ADDR_PUBLIC)
#endif

typedef struct {
    UINT8 le_addr_type;
    BD_ADDR bd_addr;
} tLE_BD_ADDR;

/* set adv parameter for BLE advertising */
typedef struct {
    UINT16 adv_int_min;  /* Minimum Advertisement interval */
    UINT16 adv_int_max;  /* Maximum Advertisement interval */
    tLE_BD_ADDR dir_bda; /* Directed Adv BLE addr type and addr */
} tBLE_ADV_PARAM;

typedef struct {
    UINT16 conn_id;
    UINT16 attr_id;
    UINT16 data_len;
    UINT8 value[GATT_MAX_ATTR_LEN];
    BOOLEAN need_confirm;
} tBLE_SE_SENDIND;

typedef struct {
    UINT16 conn_id;
    UINT32 trans_id;
    UINT8 status;   /*The status of the request to be sent to the remote devices */
    UINT16 handle;  /* attribute handle */
    UINT16 offset;  /* attribute value offset, if no offfset is needed for the command, ignore it */
    UINT16 len;     /* length of attribute value */
    UINT8 auth_req; /*  authentication request */
    UINT8 value[GATT_MAX_ATTR_LEN]; /* the actual attribute value */
} tBLE_SE_SENDRSP;

/* Attribute permissions
*/
#define BLE_GATT_PERM_READ (1 << 0)              /* bit 0 */
#define BLE_GATT_PERM_READ_ENCRYPTED (1 << 1)    /* bit 1 */
#define BLE_GATT_PERM_READ_ENC_MITM (1 << 2)     /* bit 2 */
#define BLE_GATT_PERM_WRITE (1 << 4)             /* bit 4 */
#define BLE_GATT_PERM_WRITE_ENCRYPTED (1 << 5)   /* bit 5 */
#define BLE_GATT_PERM_WRITE_ENC_MITM (1 << 6)    /* bit 6 */
#define BLE_GATT_PERM_WRITE_SIGNED (1 << 7)      /* bit 7 */
#define BLE_GATT_PERM_WRITE_SIGNED_MITM (1 << 8) /* bit 8 */

/* Characteristic properties
*/
#define BLE_GATT_CHAR_PROP_BIT_BROADCAST (1 << 0)
#define BLE_GATT_CHAR_PROP_BIT_READ (1 << 1)
#define BLE_GATT_CHAR_PROP_BIT_WRITE_NR (1 << 2)
#define BLE_GATT_CHAR_PROP_BIT_WRITE (1 << 3)
#define BLE_GATT_CHAR_PROP_BIT_NOTIFY (1 << 4)
#define BLE_GATT_CHAR_PROP_BIT_INDICATE (1 << 5)
#define BLE_GATT_CHAR_PROP_BIT_AUTH (1 << 6)
#define BLE_GATT_CHAR_PROP_BIT_EXT_PROP (1 << 7)

#define BLE_AD_BIT_DEV_NAME (0x00000001 << 0)
#define BLE_AD_BIT_FLAGS (0x00000001 << 1)
#define BLE_AD_BIT_MANU (0x00000001 << 2)
#define BLE_AD_BIT_TX_PWR (0x00000001 << 3)
#define BLE_AD_BIT_INT_RANGE (0x00000001 << 5)
#define BLE_AD_BIT_SERVICE (0x00000001 << 6)
#define BLE_AD_BIT_SERVICE_SOL (0x00000001 << 7)
#define BLE_AD_BIT_SERVICE_DATA (0x00000001 << 8)
#define BLE_AD_BIT_SIGN_DATA (0x00000001 << 9)
#define BLE_AD_BIT_SERVICE_128SOL (0x00000001 << 10)
#define BLE_AD_BIT_APPEARANCE (0x00000001 << 11)
#define BLE_AD_BIT_PUBLIC_ADDR (0x00000001 << 12)
#define BLE_AD_BIT_RANDOM_ADDR (0x00000001 << 13)
#define BLE_AD_BIT_SERVICE_32 (0x00000001 << 4)
#define BLE_AD_BIT_SERVICE_32SOL (0x00000001 << 14)
#define BLE_AD_BIT_PROPRIETARY (0x00000001 << 15)
#define BLE_AD_BIT_SERVICE_128 (0x00000001 << 16) /*128-bit Service UUIDs*/

/* ADV data flag bit definition used for BTM_BLE_AD_TYPE_FLAG */
#define BLE_LIMIT_DISC_FLAG (0x01 << 0)    /* bit 0 */
#define BLE_GEN_DISC_FLAG (0x01 << 1)      /* bit 1 */
#define BLE_BREDR_NOT_SPT (0x01 << 2)      /* bit 2*/
#define BLE_DMT_CONTROLLER_SPT (0x01 << 3) /* bit 3 */
#define BLE_DMT_HOST_SPT (0x01 << 4)       /* bit 4 */
#define BLE_NON_LIMIT_DISC_FLAG (0x00)     /* lowest bit unset */
#define BLE_ADV_FLAG_MASK (BLE_LIMIT_DISC_FLAG | BLE_BREDR_NOT_SPT | BLE_GEN_DISC_FLAG)
#define BLE_LIMIT_DISC_MASK (BLE_LIMIT_DISC_FLAG)

/**
 * \brief ble start
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_ble_start(void);

/**
 * \brief ble stop
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_ble_stop(void);

/**
 * \brief Create service
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_ble_server_create_service(ble_create_service_params *params);

/**
 * \brief Add character to service
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_ble_server_add_character(ble_char_params *params);

/**
 * \brief Start service
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_ble_server_start_service(int server_num, int srvc_attr_num);

/**
 * \brief Set the ble adv data
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_set_ble_adv_data(tBLE_ADV_CONFIG *p_data);

/**
 * \brief Set the ble advertisement interval
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_ble_set_adv_param(tBLE_ADV_PARAM *p_req);

/**
 * \brief Deregister ble server
 *
 * \return Returns an error code
 */
BtError linkplay_bluetooth_ble_server_deregister(int server_num);

/**
 * \brief Send indication to client
 *
 * \return Returns an error code
 */
BtError linkplay_ble_server_send_indication(ble_server_indication *params);

/**
 * \brief BLE SendRsp Init
 *
 * \return Returns an error code
 */

int linkplay_ble_SeSendRspInit(tBLE_SE_SENDRSP *p_sendrsp);

/**
 * \brief BLE SendRsp
 *
 * \return Returns an error code
 */
int linkplay_ble_SeSendRsp(tBLE_SE_SENDRSP *p_sendrsp);

/**
* \brief  Set the BLE Visibility parameters (InquiryScan & PageScan)
* \param[in] discoverable FALSE if not discoverable
* \param[in] connectable: FALSE if not connectable
** Returns  Returns an error code
*/
BtError linkplay_bluetooth_ble_set_visibility(BOOLEAN discoverable, BOOLEAN connectable);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
