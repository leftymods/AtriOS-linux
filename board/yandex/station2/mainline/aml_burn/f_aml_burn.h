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

/* Optimus v2 wire layout; keep offsets identical to usb_pcd.h. */
struct CmdData_WriteMedia {
 __le32 retryTimes;
 __le32 userDataLen;
 __le32 sequenceNo;
 __le32 dataCheckSum;
 __le16 dataCheckAlgorithm;
 __le16 bulkInAckLen;
 u8 reserved[12];
} __packed;

#define AML_MAX_CHUNK (1024U * 1024U)

/* Bytewise access also works on packed/unaligned host packets. */
static inline u32 aml_addsum(const u8 *p, u32 n)
{
 u32 sum = 0, i, word = 0;
 for (i = 0; i < n; i++) {
  word |= (u32)p[i] << ((i & 3) * 8);
  if ((i & 3) == 3) { sum += word; word = 0; }
 }
 return sum + word;
}

static inline int aml_valid_chunk(u32 len, u32 algorithm, u32 ack_len)
{
 return len && len <= AML_MAX_CHUNK && ack_len == 512 &&
  (algorithm == WRITE_MEDIA_CKC_ALG_NONE ||
   algorithm == WRITE_MEDIA_CKC_ALG_ADDSUM);
}
/* Subtract before comparing to avoid offset+length overflow. */
static inline int aml_valid_write(u64 offset, u32 length, u64 total, u32 block)
{
 return block && length && offset <= total && length <= total - offset &&
        !(offset % block) && !(length % block);
}
#endif
