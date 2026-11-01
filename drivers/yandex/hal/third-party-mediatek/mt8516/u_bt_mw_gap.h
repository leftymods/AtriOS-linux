/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 *
 * MediaTek Inc. (C) 2016-2017. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */

/* FILE NAME:  u_bt_mw_gap.h
 * PURPOSE:
 *  {1. What is covered in this file - function and scope.}
 *  {2. Related documents or hardware information}
 * NOTES:
 *  {Something must be known or noticed}
 *  {1. How to use these functions - Give an example.}
 *  {2. Sequence of messages if applicable.}
 *  {3. Any design limitation}
 *  {4. Any performance limitation}
 *  {5. Is it a reusable component}
 *
 *
 *
 */
#ifndef U_BT_MW_GAP_H
#define U_BT_MW_GAP_H

#include "u_bt_mw_common.h"

#define MAX_PIN_LEN                      ((UINT32)     16)

#define BT_INQUIRY_FILTER_TYPE_ALL 0xFF
#define BT_INQUIRY_FILTER_TYPE_A2DP_SRC (1<<0)
#define BT_INQUIRY_FILTER_TYPE_A2DP_SNK (1<<1)
#define BT_INQUIRY_FILTER_TYPE_HID (1<<2)
#define BT_INQUIRY_FILTER_TYPE_HFP (1<<3)
#define BT_INQUIRY_FILTER_TYPE_SPP (1<<4)

#define BT_GAP_RES_SERVICE_ID      0           /* Reserved */
#define BT_GAP_SPP_SERVICE_ID      1           /* Serial port profile. */
#define BT_GAP_DUN_SERVICE_ID      2           /* Dial-up networking profile. */
#define BT_GAP_A2DP_SOURCE_SERVICE_ID      3   /* A2DP Source profile. */
#define BT_GAP_LAP_SERVICE_ID      4           /* LAN access profile. */
#define BT_GAP_HSP_SERVICE_ID      5           /* Headset profile. */
#define BT_GAP_HFP_SERVICE_ID      6           /* Hands-free profile. */
#define BT_GAP_OPP_SERVICE_ID      7           /* Object push  */
#define BT_GAP_FTP_SERVICE_ID      8           /* File transfer */
#define BT_GAP_CTP_SERVICE_ID      9           /* Cordless Terminal */
#define BT_GAP_ICP_SERVICE_ID      10          /* Intercom Terminal */
#define BT_GAP_SYNC_SERVICE_ID     11          /* Synchronization */
#define BT_GAP_BPP_SERVICE_ID      12          /* Basic printing profile */
#define BT_GAP_BIP_SERVICE_ID      13          /* Basic Imaging profile */
#define BT_GAP_PANU_SERVICE_ID     14          /* PAN User */
#define BT_GAP_NAP_SERVICE_ID      15          /* PAN Network access point */
#define BT_GAP_GN_SERVICE_ID       16          /* PAN Group Ad-hoc networks */
#define BT_GAP_SAP_SERVICE_ID      17          /* SIM Access profile */
#define BT_GAP_A2DP_SINK_SERVICE_ID        18  /* A2DP Sink */
#define BT_GAP_AVRCP_SERVICE_ID    19          /* A/V remote control */
#define BT_GAP_HID_SERVICE_ID      20          /* HID */
#define BT_GAP_VDP_SERVICE_ID      21          /* Video distribution */
#define BT_GAP_PBAP_SERVICE_ID     22          /* PhoneBook Access Server*/
#define BT_GAP_HSP_HS_SERVICE_ID   23          /* HFP HS role */
#define BT_GAP_HFP_HS_SERVICE_ID   24          /* HSP HS role */
#define BT_GAP_MAP_SERVICE_ID      25          /* Message Access Profile */
#define BT_GAP_MN_SERVICE_ID       26          /* Message Notification Service */
#define BT_GAP_HDP_SERVICE_ID      27          /* Health Device Profile */
#define BT_GAP_PCE_SERVICE_ID      28          /* PhoneBook Access Client*/
#define BT_GAP_SDP_SERVICE_ID      29          /* SDP Search*/
#define BT_GAP_BLE_SERVICE_ID      30          /* GATT profile */

#define BT_GAP_DEVICE_TYPE_BREDR   0x01        /* For device type BREDR only */
#define BT_GAP_DEVICE_TYPE_BLE     0x02        /* For device type BLE only */
#define BT_GAP_DEVICE_TYPE_DUMO    0x03        /* For device type dual mode */



typedef enum
{
   GAP_STATE_ON = 100,
   GAP_STATE_OFF ,
   GAP_STATE_ACL_CONNECTED ,
   GAP_STATE_ACL_DISCONNECTED ,
   GAP_STATE_BONDED,
   GAP_STATE_BONDING,
   GAP_STATE_NO_BOND, /* pair fail */
   GAP_STATE_UNPAIR_SUCCESS, /* unpair success */
   GAP_STATE_DISCOVERY_STARTED,
   GAP_STATE_DISCOVERY_STOPED,
}BTMW_GAP_STATE;




typedef struct
{
    BTMW_GAP_STATE  state;
    UINT8 reason;
    CHAR bd_addr[MAX_BDADDR_LEN];
}tBTMW_GAP_STATE;

typedef struct
{
    BTMW_DEVICE_KIND device_kind;
    BTMW_DEVICE_KIND prop_mask; /* property mask */
    BLUETOOTH_DEVICE device;
}tBTMW_GAP_DEVICE_INFO;

typedef struct _BT_LOCAL_DEV
{
    CHAR               bdAddr[MAX_BDADDR_LEN];/* Bluetooth Address */
    CHAR               name[MAX_NAME_LEN];/* Name of device */
    BTMW_GAP_STATE     state;
    UINT8              scan_mode;
    UINT32             service;
}BT_LOCAL_DEV;

typedef enum pairing_key_type_s
{
    PIN_CODE = 0,
    PASSKEY,
    Key_TYPE_END
} pairing_key_type_t;

typedef struct pairing_key_value_s
{
    pairing_key_type_t key_type;
    CHAR pin_code[MAX_PIN_LEN]; /* When key_type=PIN_CODE, this value is available*/
    UINT32 key_value; /* When key_type=PASS_KEY, this value is available*/
} pairing_key_value_t;

typedef VOID (*BtAppGapEventCbk)(tBTMW_GAP_STATE *gap_event);
typedef VOID (*BtAppGapInquiryResponseCbk)(tBTMW_GAP_DEVICE_INFO* pt_result);
typedef VOID (*BtAppGapGetPairingKeyCbk)(pairing_key_value_t *bt_pairing_key, UINT8 *fg_accept);
typedef VOID (*BtAppLogOut)(char *log_str);

typedef struct _BT_APP_CB_FUNC
{
    BtAppGapEventCbk bt_gap_event_cb;
    BtAppGapGetPairingKeyCbk bt_get_pairing_key_cb;
    BtAppGapInquiryResponseCbk bt_dev_info_cb;
    BtAppLogOut bt_app_log_cb;
}BT_APP_CB_FUNC;

#endif /* End of U_BT_MW_GAP_H */

