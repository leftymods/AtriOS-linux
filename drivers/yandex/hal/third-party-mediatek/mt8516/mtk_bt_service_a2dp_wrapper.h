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

/********************************************************************************************
 *     LEGAL DISCLAIMER
 *
 *     (Header of MediaTek Software/Firmware Release or Documentation)
 *
 *     BY OPENING OR USING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 *     THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE") RECEIVED
 *     FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO BUYER ON AN "AS-IS" BASIS
 *     ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED,
 *     INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR
 *     A PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY
 *     WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 *     INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND BUYER AGREES TO LOOK
 *     ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO
 *     NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION
 *     OR TO CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
 *
 *     BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE LIABILITY WITH
 *     RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION,
 *     TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE
 *     FEES OR SERVICE charGE PAID BY BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 *     THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE WITH THE LAWS
 *     OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT OF LAWS PRINCIPLES.
 ************************************************************************************************/

#ifndef _MTK_BT_SERVICE_A2DP_WRAPPER_H_
#define _MTK_BT_SERVICE_A2DP_WRAPPER_H_

#include "u_rpcipc_types.h"
#include "u_bt_mw_a2dp.h"

#ifdef  __cplusplus
extern "C" {
#endif

typedef VOID (*mtkrpcapi_BtAppA2dpCbk)(BT_A2DP_EVENT_PARAM *param, VOID *pv_tag);

/* FUNCTION NAME: a_mtkapi_a2dp_connect
 * PURPOSE:
 *      connect A2DP to a specified device.
 * INPUT:
 *      addr        -- connect to this device
 *      local_role  -- local device role
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- connection is sent out successfully
 *      BT_ERR_STATUS_NOT_READY -- A2DP is not ready.
 *      BT_ERR_STATUS_BUSY -- it's not disconnected device or role is changing.
 *      BT_ERR_STATUS_UNSUPPORTED -- it's a wrong role
 *      others      -- connection is sent out fail.
 * NOTES:
 *      when this API return, it does not mean the connection is OK. It just
 *  indicates a A2DP connection is sent out. Caller need wait a async
 *  event:BT_A2DP_EVENT_CONNECTED.
 */
extern INT32 a_mtkapi_a2dp_connect(CHAR *addr, BT_A2DP_ROLE local_role);

/* FUNCTION NAME: a_mtkapi_a2dp_disconnect
 * PURPOSE:
 *      disconnect A2DP connection.
 * INPUT:
 *      addr        -- disconnect to this device
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- disconnect is sent out successfully
 *      others      -- disconnect is sent out fail
 * NOTES:
 *      when this API return, it does not mean the disconnection is OK. It just
 *  indicates a A2DP disconnect request is sent out. Caller need wait a async
 *  event:BT_A2DP_EVENT_DISCONNECTED.
 */
extern INT32 a_mtkapi_a2dp_disconnect(char *addr);

/* FUNCTION NAME: a_mtkapi_a2dp_register_callback
 * PURPOSE:
 *      it is used to register an event callback function. The A2DP MW event
 *  will be reported by this callback function. When user call this function
 *  will enable/disable A2DP function.
 * INPUT:
 *      a2dp_handle  -- non NULL: enable A2DP function
 *                      NULL: disable A2DP function
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- register successfully
 *      others      -- register fail
 * NOTES:
 *
 */
extern INT32 a_mtkapi_a2dp_register_callback(mtkrpcapi_BtAppA2dpCbk a2dp_handle,
    VOID* pv_tag);

/* FUNCTION NAME: a_mtkapi_a2dp_sink_adjust_buf_time
 * PURPOSE:
 *      adjust the buffer time in player. After buffer some data and then start
 *  to play the streaming data. It is used to avoid choppy sound in bad
 *  environment.
 * INPUT:
 *      buffer_time  -- the buffer time, unit: ms
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- set successfully
 *      others      -- set fail
 * NOTES:
 *
 */
extern INT32 a_mtkapi_a2dp_sink_adjust_buf_time(UINT32 buffer_time);

/* FUNCTION NAME: a_mtkapi_a2dp_sink_get_dev_list
 * PURPOSE:
 *      it is used to get source device list which have connected before. The
 *  lastest device is the first one in dev[].
 * INPUT:
 *      N/A
 * OUTPUT:
 *      dev_list  -- the source device list
 * RETURN:
 *      BT_SUCCESS  -- get successfully
 *      others      -- get fail
 * NOTES:
 *
 */
extern INT32 a_mtkapi_a2dp_sink_get_dev_list(BT_A2DP_DEVICE_LIST *dev_list);

/* FUNCTION NAME: a_mtkapi_a2dp_sink_enable
 * PURPOSE:
 *      enable A2DP sink function. Then it can connect to A2DP source device.
 * INPUT:
 *      enable  -- TRUE: enable A2DP sink function
 *                 FALSE: disable A2DP sink function
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- set successfully
 *      BT_ERR_STATUS_BUSY -- there is still connection
 *      others      -- set fail
 * NOTES:
 *
 */
extern INT32 a_mtkapi_a2dp_sink_enable(BOOL enable);

/* FUNCTION NAME: a_mtkapi_a2dp_sink_start_player
 * PURPOSE:
 *      when A2DP is connected, it is used to start player to prepare playing.
 * INPUT:
 *      N/A
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- set successfully
 *      others      -- set fail
 * NOTES:
 *      this is a async function. Caller should wait BT_A2DP_EVENT_PLAYER_EVENT
 *  to check if player is started.
 */
extern INT32 a_mtkapi_a2dp_sink_start_player(VOID);

/* FUNCTION NAME: a_mtkapi_a2dp_sink_stop_player
 * PURPOSE:
 *      when user want to stop player, it can be used to stop player event A2DP
 *  is connected.
 * INPUT:
 *      N/A
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- set successfully
 *      others      -- set fail
 * NOTES:
 *      this is a async function. Caller should wait BT_A2DP_EVENT_PLAYER_EVENT
 *  to check if player is stopped.
 */
extern INT32 a_mtkapi_a2dp_sink_stop_player(VOID);

/* FUNCTION NAME: a_mtkapi_a2dp_src_get_dev_list
 * PURPOSE:
 *      it is used to get sink device list which have connected before. The
 *  lastest device is the first one in dev[].
 * INPUT:
 *      N/A
 * OUTPUT:
 *      dev_list  -- the sink device list
 * RETURN:
 *      BT_SUCCESS  -- get successfully
 *      others      -- get fail
 * NOTES:
 *
 */
extern INT32 a_mtkapi_a2dp_src_get_dev_list(BT_A2DP_DEVICE_LIST *dev_list);

/* FUNCTION NAME: a_mtkapi_a2dp_codec_enable
 * PURPOSE:
 *      it is used to enable/disable A2DP codec
 * INPUT:
 *      codec_type --    codec , the codec is defined in the header file "u_bt_mw_a2dp.h", as follows:
 *      enable     --    true: enable the codec, false: disable the codec
 * RETURN:
 *      BT_SUCCESS  -- enable/disable successfully
 *      others      -- enable/disable fail
 * NOTES:
 *
 */
extern INT32 a_mtkapi_a2dp_codec_enable(BT_A2DP_CODEC_TYPE codec_type, BOOL enable);

/* FUNCTION NAME: a_mtkapi_a2dp_set_link_num
 * PURPOSE:
 *      it is used to set the max source and sink conntions number.
 * INPUT:
 *      local_src_num -- the max connections number for local soure role
 *      local_sink_num -- the max connections number for local sink role
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS -- set succesfully
 * NOTES:
 *      this setting only affect after source/sink enable.
 */
extern INT32 a_mtkapi_a2dp_set_link_num(INT32 local_src_num, INT32 local_sink_num);
/* FUNCTION NAME: a_mtkapi_a2dp_src_enable
 * PURPOSE:
 *      enable  -- TRUE: enable A2DP source function
 *                 FALSE: disable A2DP source function
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- set successfully
 *      BT_ERR_STATUS_BUSY -- there is still connection
 *      others      -- set fail
 * NOTES:
 *
 */
extern INT32 a_mtkapi_a2dp_src_enable(BOOL enable);

/* FUNCTION NAME: a_mtkapi_a2dp_set_dbg_flag
 * PURPOSE:
 *      it is used to set some debug flag internally.
 * INPUT:
 *      flag  -- debug flag, indicats which debug flag should be set
 *      param -- the debug parameter, some debug flag will be set as the param.
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- set successfully
 *      others      -- set fail
 * NOTES:
 *
 */
extern INT32 a_mtkapi_a2dp_set_dbg_flag(BT_A2DP_DBG_FLAG flag,
    BT_A2DP_DBG_PARAM *param);


/* FUNCTION NAME: a_mtkapi_a2dp_sink_active_src
 * PURPOSE:
 *      active a specified SRC then it can play its streaming data.
 * INPUT:
 *      addr    -- connect to this device
 *      active  -- active or deactive, TRUE: active, FALSE: deactive
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- acitve/deactive successfully
 *      BT_ERR_STATUS_NOT_READY -- a2dp role is not enable or power if off or
 *                                  role is changing or not connected.
 *      others      -- acitve/deactive fail.
 * NOTES:
 *      N/A
 */
extern INT32 a_mtkapi_a2dp_sink_active_src(CHAR *addr, BOOL active);

/* FUNCTION NAME: a_mtkapi_a2dp_src_active_sink
 * PURPOSE:
 *      active a specified SINK then it can recv streaming data.
 * INPUT:
 *      addr    -- connect to this device
 *      active  -- active or deactive, TRUE: active, FALSE: deactive
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- acitve/deactive successfully
 *      BT_ERR_STATUS_NOT_READY -- a2dp role is not enable or power if off or
 *                                  role is changing or not connected.
 *      others      -- acitve/deactive fail.
 * NOTES:
 *      N/A
 */
extern INT32 a_mtkapi_a2dp_src_active_sink(CHAR *addr, BOOL active);
/* FUNCTION NAME: a_mtkapi_a2dp_sink_player_load
 * PURPOSE:
 *      register sink player by loading sink player so dynamic
 * INPUT:
 *      player so path    -- load the sink player
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- player register success
 *      BT_ERR_STATUS_NOT_READY -- player register fail
 * NOTES:
 *      N/A
 */
extern INT32 a_mtkapi_a2dp_sink_player_load(CHAR* player_so_path);

/* FUNCTION NAME: a_mtkapi_a2dp_sink_player_unload
 * PURPOSE:
 *      unregister sink player
 * INPUT:
 *      A2DP player name    -- sink player that will be unregistered
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS  -- player unregister success
 *      BT_ERR_STATUS_NOT_READY -- player unregister fail
 * NOTES:
 *      N/A
 */
extern INT32 a_mtkapi_a2dp_sink_player_unload(CHAR *player_name);

/* FUNCTION NAME: a_mtkapi_a2dp_src_mute_uploader
 * PURPOSE:
 *      mute uploader, then it will send streaming data out but all mute data.
 * INPUT:
 *      mute : true-mute, false-unmute
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS         -- resume successfully
 *      BT_ERR_STATUS_UNSUPPORTED -- uploader is not enable
 *      others             -- resume fail
 * NOTES:
 *      N/A
 */
extern INT32 a_mtkapi_a2dp_src_mute_uploader(BOOL mute);

/* FUNCTION NAME: a_mtkapi_a2dp_src_resume_uploader
 * PURPOSE:
 *      resume uploader, then it will send streaming data out.
 * INPUT:
 *      N/A
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS         -- resume successfully
 *      BT_ERR_STATUS_UNSUPPORTED -- uploader is not enable
 *      others             -- resume fail
 * NOTES:
 *      N/A
 */
extern INT32 a_mtkapi_a2dp_src_resume_uploader(VOID);

/* FUNCTION NAME: a_mtkapi_a2dp_src_pause_uploader
 * PURPOSE:
 *      pause uploader, then it will not send streaming data out.
 * INPUT:
 *      N/A
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS         -- pause successfully
 *      BT_ERR_STATUS_UNSUPPORTED -- uploader is not enable
 *      others             -- pause fail
 * NOTES:
 *      N/A
 */
extern INT32 a_mtkapi_a2dp_src_pause_uploader(VOID);

/* FUNCTION NAME: a_mtkapi_a2dp_sound_mute
 * PURPOSE:
 *      mute ,  It will reduce the playing data to the minimum volume value 
 *      unmute ,  It will increase the playing data to the Maximum volume value
 *      default unmute
 * INPUT:
 *      mute : true-mute, false-unmute
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS         --  successfully
 *      others             --  fail
 * NOTES:
 *      N/A
 */

extern INT32 a_mtkapi_a2dp_sound_mute(BOOL mute);

/* FUNCTION NAME: a_mtkapi_a2dp_sound_set_fade
 * PURPOSE:
 *       set the a2dp sink stream fade params
 * INPUT:
 *      maxvol : Maximum volume value , refer to the GainMap
 *      minvol : Minimum  volume value , refer to the GainMap
 *      fadeoutStep: The value fadeOut decrease each fadeoutEachFrames
 *      fadeoutEachFrames : the frames for each fadeout step
 *      fadeInStep : The value fadeIn increase each fadeInEachFrames
 *      fadeInEachFrames : the frames for each fadeIn step
 * OUTPUT:
 *      N/A
 * RETURN:
 *      BT_SUCCESS         -- successfully
 *      others             --  fail
 * NOTES:
 *      N/A
 */
/*
const unsigned int GainMap[] = {
	0x10000, //
	0xff00,  // 0: -0.50 dB
	0xf100,  // 1: -1.00 dB
	0xf000,  // 2: -1.50 dB
	0xee00,  // 3: -2.00 dB
	0xec00,  // 4: -2.50 dB
	0xea00,  // 5: -3.00 dB
	0xe700,  // 6: -3.50 dB
	0xe200,  // 7: -4.00 dB
	0xdf00,  // 8: -4.50 dB
	0xd800,  // 9: -5.00 dB
	0xd000,  // 10: -5.50 dB
	0xca00,  // 11: -6.00 dB
	0xc200,  // 12: -6.50 dB
	0xbe00,  // 13: -7.00 dB
	0xba00,  // 14: -7.50 dB
	0xb200,  // 15: -8.00 dB
	0xa900,  // 16: -8.50 dB
	0xa000,  // 17: -9.00 dB
	0x9800,  // 18: -9.50 dB
	0x9000,  // 19: -10.00 dB
	0x8800,  // 20: -10.50 dB
	0x8000,  // 21: -11.00 dB
	0x7800,  // 22: -11.50 dB
	0x7000,  // 23: -12.00 dB
	0x6200,  // 24: -12.50 dB
	0x5800,  // 25: -13.00 dB
	0x5200,  // 26: -13.50 dB
	0x4f00,  // 27: -14.00 dB
	0x4d00,  // 28: -14.50 dB
	0x4c00,  // 29: -15.00 dB
	0x4b00,  // 30: -15.50 dB
	0x4a00,  // 31: -16.00 dB
	0x4900,  // 32: -16.50 dB
	0x4800,  // 33: -17.00 dB
	0x4700,  // 34: -17.50 dB
	0x4600,  // 35: -18.00 dB
	0x4500,  // 36: -18.50 dB
	0x4400,  // 37: -19.00 dB
	0x4300,  // 38: -19.50 dB
	0x4200,  // 39: -20.00 dB
	0x4100,  // 40: -20.50 dB
	0x4000,  // 41: -21.00 dB
	0x3f00,  // 42: -21.50 dB
	0x3e00,  // 43: -22.00 dB
	0x3d00,  // 44: -22.50 dB
	0x3c00,  // 45: -23.00 dB
	0x3b00,  // 46: -23.50 dB
	0x3a00,  // 47: -24.00 dB
	0x3900,  // 48: -24.50 dB
	0x3800,  // 49: -25.00 dB
	0x3700,  // 50: -25.50 dB
	0x3600,  // 51: -26.00 dB
	0x3500,  // 52: -26.50 dB
	0x3400,  // 53: -27.00 dB
	0x3300,  // 54: -27.50 dB
	0x3200,  // 55: -28.00 dB
	0x3100,  // 56: -28.50 dB
	0x3000,  // 57: -29.00 dB
	0x2f00,  // 58: -29.50 dB
	0x2e00,  // 59: -30.00 dB
	0x2d00,  // 60: -30.50 dB
	0x2c00,  // 61: -31.00 dB
	0x2b00,  // 62: -31.50 dB
	0x2a00,  // 63: -32.00 dB
	0x2900,  // 64: -32.50 dB
	0x2800,  // 65: -33.00 dB
	0x2700,  // 66: -33.50 dB
	0x2600,  // 67: -34.00 dB
	0x2500,  // 68: -34.50 dB
	0x2400,  // 69: -35.00 dB
	0x2300,  // 70: -35.50 dB
	0x2200,  // 71: -36.00 dB
	0x2100,  // 72: -36.50 dB
	0x2000,  // 73: -37.00 dB
	0x1f00,  // 74: -37.50 dB
	0x1e50,  // 75: -38.00 dB
	0x1d40,  // 76: -38.50 dB
	0x1ca0,  // 77: -39.00 dB
	0x1b30,  // 78: -39.50 dB
	0x1af0,  // 79: -40.00 dB
	0x1950,  // 80: -40.50 dB
	0x17f0,  // 81: -41.00 dB
	0x15a0,  // 82: -41.50 dB  
	0x1420,  // 83: -42.00 dB
	0x13e0,  // 84: -42.50 dB
	0x13c0,  // 85: -43.00 dB
	0x1380,  // 86: -43.50 dB
	0x1300,  // 87: -44.00 dB
	0x12a0,  // 88: -44.50 dB
	0x1200,  // 89: -45.00 dB
	0x11a0,  // 90: -45.50 dB
	0x1100,  // 91: -46.00 dB
	0x0fff,  // 92: -46.50 dB
	0x0f00,  // 93: -47.00 dB
	0x0e00,  // 94: -47.50 dB
	0x0c00,  // 95: -48.00 dB
	0x0818,  // 96: -30.00 dB
	0x044c,  // 97: -35.00 dB
	0x026a,  // 98: -49.50 dB
	0x0170,  // 99: -50.00 dB
};
*/

extern INT32 a_mtkapi_a2dp_sound_set_fade(int maxvol,
    int minvol,
    int fadeoutStep,
    int fadeoutEachFrames,
    int fadeInStep,
    int fadeInEachFrames
);

extern INT32 c_rpc_reg_mtk_bt_service_a2dp_cb_hndlrs(VOID);

#ifdef  __cplusplus
}
#endif
#endif
