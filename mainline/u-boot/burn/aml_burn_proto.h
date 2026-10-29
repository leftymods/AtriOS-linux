/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Amlogic USB Burning Tool ("v2" burn protocol) - internal definitions.
 *
 * Protocol reference: mainline/docs/uboot-burn.md (vendor sources
 * bl33/drivers/usb/gadget/v2_burning/{v2_usb_tool,v2_common}).
 */

#ifndef __AML_BURN_PROTO_H__
#define __AML_BURN_PROTO_H__

#include <blk.h>
#include <linux/sizes.h>
#include <part.h>

/* USB identity: the Windows tool and pyamlboot match exactly on this pair. */
#define AML_BURN_VID			0x1b8e
#define AML_BURN_PID			0xc003
#define AML_BURN_BCD_DEVICE		0x0007
#define AML_BURN_BCD_USB		0x0200

#define AML_BURN_EP0_MPS		64
#define AML_BURN_BULK_EP_IN		0x81
#define AML_BURN_BULK_EP_OUT		0x02
#define AML_BURN_BULK_MPS_HS		512
#define AML_BURN_BULK_MPS_FS		64

/* One bulk "slot": the host never sends more than 64 KiB per 0x32 packet. */
#define AML_BURN_SLOT_SZ		(64 * 1024)
/* Largest reply the protocol uses (0x31/0x34 result, 0x32 ack). */
#define AML_BURN_REPLY_SZ		512
/* Command string buffer, must be NUL terminated (vendor CMD_BUFF_SIZE). */
#define AML_BURN_CMD_SZ			512
#define AML_BURN_PART_NAME_SZ		32

/* identify (0x20): {ver_major, ver_minor, stage_major, stage_minor} */
#define AML_BURN_ROM_VER_MAJOR		0
#define AML_BURN_ROM_VER_MINOR		7
#define AML_BURN_ROM_STAGE_MAJOR	0
/*
 * 0 = IPL/BootROM, 8 = SPL, 16 = TPL (U-Boot).  Must stay 16, otherwise the
 * tool tries to download the DDR firmware and BL2 over AMLC.
 */
#define AML_BURN_ROM_STAGE_MINOR	16
#define AML_BURN_IDENTIFY_LEN		8

/* Vendor requests, see vendor usb_pcd.h. */
#define AM_REQ_WRITE_MEM		0x01
#define AM_REQ_READ_MEM			0x02
#define AM_REQ_FILL_MEM			0x03
#define AM_REQ_MODIFY_MEM		0x04
#define AM_REQ_RUN_IN_ADDR		0x05
#define AM_REQ_WRITE_AUX		0x06
#define AM_REQ_READ_AUX			0x07
#define AM_REQ_WR_LARGE_MEM		0x11
#define AM_REQ_RD_LARGE_MEM		0x12
#define AM_REQ_IDENTIFY_HOST		0x20
#define AM_REQ_TPL_CMD			0x30
#define AM_REQ_TPL_STAT			0x31
#define AM_REQ_DOWNLOAD			0x32
#define AM_REQ_UPLOAD			0x33
#define AM_REQ_BULKCMD			0x34

/* Checksum algorithms carried in the 0x32 command block. */
#define AML_CKC_ALG_NONE		0x00ee
#define AML_CKC_ALG_ADDSUM		0x00ef
#define AML_CKC_ALG_CRC32		0x00f0

/*
 * Upload (readback, 0x33): the EP0 data stage returns two u32 words, a magic
 * and the length of the bulk IN transfer that follows, see vendor
 * optimus_buf_manager_get_command_data_for_upload_transfer().
 */
#define AML_BURN_UPLOAD_MAGIC		0xefe8
#define AML_BURN_UPLOAD_HDR_SZ		16

/*
 * AO secure configuration word (Platform:Encrypt_reg in the vendor
 * platform.conf, bit 4 = secure boot).  It sits behind the secure APB, a
 * plain read aborts the bus, so the upload path reports zero ("not secure").
 */
#define AML_BURN_SECURE_REG		0xff800228

/* burn_complete choices, see vendor optimus_download.h. */
#define AML_BURN_COMPLETE_POWEROFF_DIRECT	0x00
#define AML_BURN_COMPLETE_REBOOT_NORMAL		0x01
#define AML_BURN_COMPLETE_POWEROFF_AFTER_POWERKEY 0x02
#define AML_BURN_COMPLETE_POWEROFF_AFTER_UNPLUG	0x03
#define AML_BURN_COMPLETE_REBOOT_SDC_BURN	0xdc
#define AML_BURN_COMPLETE_REBOOT_UPDATE		0xeb
#define AML_BURN_COMPLETE_QUERY			0xe1

/*
 * Command block of the "new protocol" 0x32 request (EP0 OUT data, 32 bytes,
 * packed to 4).  Older hosts send no algorithm/ack length: they use ADDSUM
 * and 512 byte acknowledgements.
 */
struct aml_burn_write_media {
	u32 retry_times;
	u32 user_data_len;	/* bytes of bulk OUT data that follows */
	u32 sequence_no;	/* packet number, starts at 0 for every image */
	u32 data_check_sum;
	u16 data_check_alg;
	u16 bulk_in_ack_len;
	u8 reserved[12];
} __packed;

enum aml_burn_img_kind {
	AML_IMG_NONE = 0,
	AML_IMG_NORMAL,		/* raw partition image */
	AML_IMG_DTB,		/* _aml_dtb / dtb partition */
	AML_IMG_BOOTLOADER,	/* FIP (BL2+BL33), streamed to "bootloader" */
	AML_IMG_SPARSE,		/* AOSP sparse image */
};

/* Streaming AOSP sparse decoder state. */
enum aml_burn_sparse_stage {
	AML_SPARSE_FILE_HDR = 0,
	AML_SPARSE_CHUNK_HDR,
	AML_SPARSE_RAW,
	AML_SPARSE_FILL_PAT,
	AML_SPARSE_CRC,
};

struct aml_burn_sparse {
	u8 stage;
	u32 blk_sz;
	u32 total_chunks;
	u32 chunks_done;
	u32 chunk_type;
	u64 out_off;		/* byte offset in the expanded image */
	u32 out_left;		/* bytes of the current chunk still to emit */
	u8 fill[4];
	u8 carry[32];		/* header/fill bytes spanning packets */
	u32 carry_len;
	u32 need;		/* bytes `carry' has to collect, 0 = none */
};

struct aml_burn_img {
	bool active;
	bool complete;
	bool failed;
	int kind;
	char part[AML_BURN_PART_NAME_SZ];
	char type[16];
	u64 total;		/* image size announced by the host */
	u64 wire;		/* bytes accepted from the host so far */
	u32 next_seq;		/* next expected 0x32 sequence number */
	u64 media_off;		/* bytes written (normal images) */
	struct aml_burn_sparse sparse;
};

/* Readback (0x33): where the bytes the host asks for come from. */
enum aml_upload_kind {
	AML_UP_NONE = 0,
	AML_UP_MEM,		/* `upload mem <addr> ...' */
	AML_UP_STORE,		/* `upload store <part> ...' */
};

struct aml_burn_upload {
	bool active;
	int kind;
	u64 addr;		/* AML_UP_MEM: first byte to read */
	u64 total;		/* bytes requested by the host */
	u64 done;		/* bytes already sent */
};

/*
 * Amlogic factory partition table ("MPT", vendor struct ptbl_rsv in
 * drivers/mmc/aml_emmc_partition.c).  It lives at the start of the inherent
 * "reserved" partition, i.e. 4 MiB (bootloader) + 32 MiB (gap) = 36 MiB.
 * Entries carry names and sizes only; the offsets are derived from the sizes
 * and the 8 MiB gap between partitions, exactly like the vendor does in
 * _calculate_offset().
 */
#define AML_PTBL_MAX_PARTS	32
#define AML_PTBL_MAGIC		"MPT"
#define AML_PTBL_VERSION_1	"01.00.00"
#define AML_PTBL_VERSION_2	"01.02.00"
#define AML_PTBL_BOOT_SIZE	(4 * SZ_1M)
#define AML_PTBL_BOOT_GAP	(32 * SZ_1M)
#define AML_PTBL_OFFSET		(AML_PTBL_BOOT_SIZE + AML_PTBL_BOOT_GAP)
#define AML_PTBL_GAP		(8 * SZ_1M)
#define AML_PTBL_NAME_SZ	16
/* sizeof(struct aml_ptbl) == 1304, rounded up to whole sectors */
#define AML_PTBL_READ_SZ	2048

struct aml_ptbl_entry {
	u8 name[AML_PTBL_NAME_SZ];
	u64 size;		/* bytes, ~0ull = "rest of the device" */
	u64 offset;		/* computed, the stored value is ignored */
	u32 mask_flags;
	u32 reserved;		/* tail padding of the vendor struct */
};

struct aml_ptbl {
	u8 magic[4];
	u8 version[12];
	u32 count;
	u32 checksum;
	struct aml_ptbl_entry part[AML_PTBL_MAX_PARTS];
};

/* aml_burn_storage.c */
int aml_store_init(void);
void aml_store_exit(void);
int aml_store_select(const char *name);
int aml_img_begin(const char *media, const char *part, const char *type,
		  u64 size, struct aml_burn_img *img);
int aml_img_write(struct aml_burn_img *img, const u8 *data, u32 len);
int aml_img_verify_sha1(const struct aml_burn_img *img, const char *hex);
int aml_up_begin(const char *media, const char *part, const char *type,
		 u64 size, struct aml_burn_upload *up);
int aml_up_read(struct aml_burn_upload *up, u64 off, u8 *buf, u32 len);

/* aml_burn.c, shared with the validators in the protocol code */
u32 aml_burn_add_sum(const void *data, u32 len);

#endif /* __AML_BURN_PROTO_H__ */
