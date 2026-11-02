/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __F_AML_BURN_H
#define __F_AML_BURN_H

#include <linux/types.h>

#define AM_REQ_WRITE_MEM	0x01
#define AM_REQ_READ_MEM		0x02
#define AM_REQ_RUN_IN_ADDR	0x05
#define AM_REQ_IDENTIFY_HOST	0x20
#define AM_REQ_TPL_CMD		0x30
#define AM_REQ_TPL_STAT		0x31
#define AM_REQ_DOWNLOAD		0x32
#define AM_REQ_UPLOAD		0x33
#define AM_REQ_BULKCMD		0x34
#define AM_REQ_NOP		0x36

#define WRITE_MEDIA_CKC_ALG_ADDSUM	0x00ef
#define WRITE_MEDIA_CKC_ALG_NONE	0x00ee

struct CmdData_WriteMedia {
	__le32 mediaType;
	__le32 ckcAlg;		/* checksum algorithm */
	__le32 ckcValue;	/* expected checksum */
	__le32 userDataLen;
	__le32 reserved[4];
} __packed;			/* 32 bytes */

#endif
