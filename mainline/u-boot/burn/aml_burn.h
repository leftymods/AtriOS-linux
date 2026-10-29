/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Amlogic USB Burning Tool ("v2" burn protocol) support.
 *
 * Public entry point.  Ported from the Amlogic vendor BSP
 * (bl33/drivers/usb/gadget/v2_burning, usb_pcd.c / optimus_*.c), see
 * mainline/docs/uboot-burn-port.md and mainline/docs/uboot-burn.md.
 */

#ifndef __AML_BURN_H__
#define __AML_BURN_H__

/**
 * aml_usb_burn_run() - run the Amlogic USB burn gadget until the host is done
 *
 * @connect_timeout_ms: how long to wait for the first control request from
 *			the host, 0 selects the built-in default (5000 ms)
 * @identify_timeout_ms: how long to wait for the AM_REQ_IDENTIFY_HOST vendor
 *			request (0x20) after SET_CONFIGURATION, 0 waits forever
 *
 * The device enumerates as 1b8e:c003 with one vendor specific interface and
 * two bulk endpoints (0x81 IN / 0x02 OUT, 512 byte packets).  The function
 * returns when the host issues burn_complete/reset and the reply has been
 * sent, when the timeouts expire, or on Ctrl-C.
 *
 * Return: 0 on success (burn finished), -ETIMEDOUT if no host showed up,
 *	   -EINTR on Ctrl-C, other negative errno on setup failures.
 */
int aml_usb_burn_run(unsigned int connect_timeout_ms,
		     unsigned int identify_timeout_ms);

#endif /* __AML_BURN_H__ */
