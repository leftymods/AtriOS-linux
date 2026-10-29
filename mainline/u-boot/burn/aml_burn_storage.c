// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic USB Burning Tool ("v2" burn protocol) - storage backend.
 *
 * Replaces the vendor "store" layer (store_write_ops()/store_boot_write()/
 * store_dtb_rw(), which speak the Amlogic proprietary partition table) with
 * the mainline block layer: blk_dwrite()/blk_dread() on a partition looked up
 * by name with part_get_info_by_name().
 *
 * Devices that still carry the vendor (raw "store") partition table do not
 * expose a GPT/MBR, so the vendor "MPT" table is parsed from the start of the
 * inherent "reserved" partition (byte offset 36 MiB) and, as a last resort, an
 * environment override is used:
 *
 *	aml_burn_part_<name>=<start>:<size>	(units of blk_desc->blksz)
 *
 * e.g. aml_burn_part_boot=0x800:0x20000.  See docs/uboot-burn-port.md.
 */

#include <blk.h>
#include <env.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <malloc.h>
#include <memalign.h>
#include <part.h>
#include <sparse_format.h>
#include <u-boot/sha1.h>
#include <vsprintf.h>

#include "aml_burn_proto.h"

#define AML_BURN_SHA1_CHUNK	AML_BURN_SLOT_SZ

static struct blk_desc *store_desc;
static struct disk_partition store_part;
static bool store_part_valid;
static char store_part_name[AML_BURN_PART_NAME_SZ];

static u8 *store_wr_bounce;	/* AML_BURN_SLOT_SZ, DMA aligned */
static u8 *store_rd_bounce;	/* AML_BURN_SLOT_SZ, DMA aligned */

/* Vendor partition table, parsed once per storage (re-read on disk_initial). */
static struct aml_ptbl store_ptbl;
static bool store_ptbl_valid;
static u8 store_ptbl_buf[AML_PTBL_READ_SZ] __aligned(ARCH_DMA_MINALIGN);

static u64 store_part_bytes(void)
{
	if (!store_part_valid)
		return 0;

	return (u64)store_part.size * store_part.blksz;
}

int aml_store_init(void)
{
	const char *ifname = env_get("aml_burn_ifname");
	const char *devstr = env_get("aml_burn_dev");
	struct blk_desc *desc = NULL;
	int ret;

	if (!ifname)
		ifname = "mmc";
	if (!devstr)
		devstr = "0";

	ret = blk_get_device_by_str(ifname, devstr, &desc);
	if (ret || !desc) {
		printf("aml_burn: no block device %s %s (ret=%d)\n",
		       ifname, devstr, ret);
		return ret ? ret : -ENODEV;
	}

	store_desc = desc;

	if (!store_wr_bounce) {
		store_wr_bounce = memalign(ARCH_DMA_MINALIGN,
					   AML_BURN_SLOT_SZ);
		store_rd_bounce = memalign(ARCH_DMA_MINALIGN,
					   AML_BURN_SLOT_SZ);
		if (!store_wr_bounce || !store_rd_bounce)
			return -ENOMEM;
	}

	printf("aml_burn: storage %s %s: %llu x %lu B\n", ifname, devstr,
	       (unsigned long long)desc->lba,
	       (unsigned long)desc->blksz);

	return 0;
}

void aml_store_exit(void)
{
	store_part_valid = false;
	store_part_name[0] = '\0';
	store_ptbl_valid = false;
}

/* ------------------------------------------------------------------ */
/* Amlogic factory ("MPT") partition table				*/
/* ------------------------------------------------------------------ */

/*
 * Vendor checksum, version 1 has the bug the tables on the media were
 * generated with: it sums the first entry `count' times.  Do not fix it, the
 * stored value was computed the buggy way (see _calc_iptbl_check_v1()).
 */
static u32 ptbl_checksum(const struct aml_ptbl *t, int version)
{
	const u32 *p = (const u32 *)t->part;
	u32 sum = 0;
	int i, j;

	if (version == 2) {
		for (i = 0; i < (int)t->count *
			     (int)(sizeof(struct aml_ptbl_entry) / 4); i++)
			sum += p[i];
		return sum;
	}

	for (i = 0; i < (int)t->count; i++)
		for (j = 0; j < (int)(sizeof(struct aml_ptbl_entry) / 4); j++)
			sum += p[j];

	return sum;
}

/*
 * Offsets are not stored: the vendor derives them from the sizes, with a
 * 32 MiB gap after "bootloader" and 8 MiB between the other partitions.  A
 * size of ~0ull means "everything up to the end of the device".
 */
static int ptbl_offsets(struct aml_ptbl *t, u64 capacity)
{
	u64 gap = AML_PTBL_BOOT_GAP;
	u64 end;
	int i;

	t->part[0].offset = 0;
	if (memcmp(t->part[0].name, "bootloader", sizeof("bootloader")))
		gap = AML_PTBL_GAP;

	for (i = 1; i < (int)t->count; i++) {
		struct aml_ptbl_entry *p = &t->part[i];

		p->offset = t->part[i - 1].offset + t->part[i - 1].size + gap;
		end = p->offset + p->size;
		if (p->size == ~0ULL || end < p->offset || end > capacity) {
			if (p->offset >= capacity)
				return -EINVAL;
			p->size = capacity - p->offset;
			t->count = i + 1;
			break;
		}
		gap = AML_PTBL_GAP;
	}

	return 0;
}

static int ptbl_parse(struct blk_desc *desc)
{
	struct aml_ptbl *t = (struct aml_ptbl *)store_ptbl_buf;
	u64 capacity = (u64)desc->lba * desc->blksz;
	lbaint_t blk;
	u32 blocks;
	int version, i;

	if (capacity < AML_PTBL_OFFSET + sizeof(*t)) {
		debug("aml_burn: eMMC too small for an Amlogic table\n");
		return -ENOSPC;
	}

	blk = AML_PTBL_OFFSET / desc->blksz;
	blocks = DIV_ROUND_UP(sizeof(*t), desc->blksz);
	if (blocks * desc->blksz > sizeof(store_ptbl_buf)) {
		printf("aml_burn: block size %u is too large for the table buffer\n",
		       desc->blksz);
		return -ENOTSUPP;
	}
	if (blk_dread(desc, blk, blocks, store_ptbl_buf) != blocks) {
		printf("aml_burn: cannot read the Amlogic partition table\n");
		return -EIO;
	}

	if (memcmp(t->magic, AML_PTBL_MAGIC, sizeof(AML_PTBL_MAGIC))) {
		debug("aml_burn: no Amlogic partition table (%4.4s)\n",
		      t->magic);
		return -ENOENT;
	}

	if (!memcmp(t->version, AML_PTBL_VERSION_1, 8)) {
		version = 1;
	} else if (!memcmp(t->version, AML_PTBL_VERSION_2, 8)) {
		version = 2;
	} else {
		printf("aml_burn: unsupported Amlogic table version %8.8s\n",
		       t->version);
		return -ENOENT;
	}

	if (!t->count || t->count > AML_PTBL_MAX_PARTS) {
		printf("aml_burn: bad Amlogic table partition count %u\n",
		       t->count);
		return -EINVAL;
	}

	if (ptbl_checksum(t, version) != t->checksum) {
		printf("aml_burn: Amlogic table checksum mismatch (0x%x != 0x%x)\n",
		       ptbl_checksum(t, version), t->checksum);
		return -EINVAL;
	}

	for (i = 0; i < (int)t->count; i++) {
		if (!memchr(t->part[i].name, '\0', AML_PTBL_NAME_SZ)) {
			printf("aml_burn: Amlogic table entry %d has no name\n",
			       i);
			return -EINVAL;
		}
	}

	if (ptbl_offsets(t, capacity))
		return -EINVAL;

	memcpy(&store_ptbl, t, sizeof(store_ptbl));
	printf("aml_burn: Amlogic partition table v%d, %u partitions\n",
	       version, store_ptbl.count);

	return 0;
}

static int store_select_ptbl(const char *name)
{
	struct blk_desc *desc = store_desc;
	int i;

	if (!store_ptbl_valid) {
		if (ptbl_parse(desc))
			return -ENOENT;
		store_ptbl_valid = true;
	}

	for (i = 0; i < (int)store_ptbl.count; i++) {
		const struct aml_ptbl_entry *p = &store_ptbl.part[i];

		if (strncmp((const char *)p->name, name, AML_PTBL_NAME_SZ))
			continue;

		memset(&store_part, 0, sizeof(store_part));
		store_part.start = p->offset / desc->blksz;
		store_part.size = p->size / desc->blksz;
		store_part.blksz = desc->blksz;
		strlcpy((char *)store_part.name, name, sizeof(store_part.name));

		printf("aml_burn: partition %s from Amlogic table: 0x%llx+0x%llx\n",
		       name, (unsigned long long)p->offset,
		       (unsigned long long)p->size);

		return 0;
	}

	return -ENOENT;
}

/* Partition lookup by name, with the env override for unknown layouts. */
static int store_select_env(const char *name)
{
	char var[64];
	const char *val;
	char *end;
	u64 start, size;

	snprintf(var, sizeof(var), "aml_burn_part_%s", name);
	val = env_get(var);
	if (!val)
		return -ENOENT;

	start = simple_strtoull(val, &end, 0);
	if (end == val || *end != ':')
		return -EINVAL;
	size = simple_strtoull(end + 1, &end, 0);
	if (end == val || size == 0)
		return -EINVAL;

	memset(&store_part, 0, sizeof(store_part));
	store_part.start = start;
	store_part.size = size;
	store_part.blksz = store_desc->blksz;
	strlcpy((char *)store_part.name, name, sizeof(store_part.name));

	printf("aml_burn: partition %s from %s: %llu+%llu blocks\n", name,
	       var, (unsigned long long)start, (unsigned long long)size);

	return 0;
}

int aml_store_select(const char *name)
{
	int ret;

	if (!name || !*name || strlen(name) >= AML_BURN_PART_NAME_SZ)
		return -EINVAL;

	if (!store_desc) {
		ret = aml_store_init();
		if (ret)
			return ret;
	}

	ret = part_get_info_by_name(store_desc, name, &store_part);
	if (ret < 0)
		ret = store_select_ptbl(name);
	if (ret < 0)
		ret = store_select_env(name);
	if (ret) {
		printf("aml_burn: partition \"%s\" not found (no GPT/MBR, no Amlogic table entry; set aml_burn_part_%s)\n",
		       name, name);
	}

	if (ret < 0) {
		store_part_valid = false;
		return -ENOENT;
	}

	store_part_valid = true;
	strlcpy(store_part_name, name, sizeof(store_part_name));
	printf("aml_burn: partition %s: start 0x%llx, %llu blocks of %lu B\n",
	       name, (unsigned long long)store_part.start,
	       (unsigned long long)store_part.size,
	       (unsigned long)store_part.blksz);

	return 0;
}

/*
 * Read/write `len' bytes at byte offset `off' inside the selected partition.
 * Writes are split so that every DMA buffer handed to the block layer is
 * cache line aligned.
 */
static int store_write(u64 off, const u8 *data, u32 len)
{
	struct blk_desc *desc = store_desc;
	u32 blksz = store_part.blksz;
	lbaint_t blk;
	u32 head;
	const u8 *src = data;

	if (!store_part_valid || !desc)
		return -ENODEV;
	if (!len)
		return 0;
	if (off + len > store_part_bytes()) {
		printf("aml_burn: write 0x%llx+0x%x outside partition %s (0x%llx B)\n",
		       (unsigned long long)off, len, store_part_name,
		       (unsigned long long)store_part_bytes());
		return -ENOSPC;
	}

	blk = store_part.start + off / blksz;
	head = off % blksz;

	if (head) {
		u32 n = min_t(u32, len, blksz - head);

		if (blk_dread(desc, blk, 1, store_wr_bounce) != 1)
			return -EIO;
		memcpy(store_wr_bounce + head, src, n);
		if (blk_dwrite(desc, blk, 1, store_wr_bounce) != 1)
			return -EIO;
		src += n;
		len -= n;
		blk++;
	}

	while (len) {
		u32 chunk = min_t(u32, len, AML_BURN_SLOT_SZ);
		u32 blocks = chunk / blksz;
		const u8 *buf;

		if (blocks) {
			u32 nbytes = blocks * blksz;

			if ((unsigned long)src % ARCH_DMA_MINALIGN) {
				memcpy(store_wr_bounce, src, nbytes);
				buf = store_wr_bounce;
			} else {
				buf = src;
			}
			if (blk_dwrite(desc, blk, blocks, buf) != blocks)
				return -EIO;
			src += nbytes;
			len -= nbytes;
			blk += blocks;
			continue;
		}

		/* trailing partial block: read-modify-write */
		if (blk_dread(desc, blk, 1, store_wr_bounce) != 1)
			return -EIO;
		memcpy(store_wr_bounce, src, len);
		if (blk_dwrite(desc, blk, 1, store_wr_bounce) != 1)
			return -EIO;
		len = 0;
	}

	return 0;
}

static int store_read(u64 off, u8 *data, u32 len)
{
	struct blk_desc *desc = store_desc;
	u32 blksz = store_part.blksz;
	lbaint_t blk;
	u32 head;

	if (!store_part_valid || !desc)
		return -ENODEV;
	if (!len)
		return 0;
	if (off + len > store_part_bytes())
		return -ENOSPC;

	blk = store_part.start + off / blksz;
	head = off % blksz;

	if (head) {
		u32 n = min_t(u32, len, blksz - head);

		if (blk_dread(desc, blk, 1, store_wr_bounce) != 1)
			return -EIO;
		memcpy(data, store_wr_bounce + head, n);
		data += n;
		len -= n;
		blk++;
	}

	while (len) {
		u32 blocks = min_t(u32, len / blksz,
				   AML_BURN_SLOT_SZ / blksz);

		if (!blocks) {
			if (blk_dread(desc, blk, 1, store_wr_bounce) != 1)
				return -EIO;
			memcpy(data, store_wr_bounce, len);
			len = 0;
			break;
		}

		if (blk_dread(desc, blk, blocks, store_rd_bounce) != blocks)
			return -EIO;
		memcpy(data, store_rd_bounce, blocks * blksz);
		data += blocks * blksz;
		len -= blocks * blksz;
		blk += blocks;
	}

	return 0;
}

static int store_write_fill(u64 off, const u8 *pattern, u32 len)
{
	u32 i;

	for (i = 0; i + 4 <= AML_BURN_SLOT_SZ; i += 4)
		memcpy(store_wr_bounce + i, pattern, 4);

	while (len) {
		u32 chunk = min_t(u32, len, AML_BURN_SLOT_SZ);
		int ret;

		ret = store_write(off, store_wr_bounce, chunk);
		if (ret)
			return ret;
		off += chunk;
		len -= chunk;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* AOSP sparse streaming decoder					*/
/* ------------------------------------------------------------------ */

static void sparse_need_hdr(struct aml_burn_sparse *s, u8 stage, u32 need)
{
	s->stage = stage;
	s->need = need;
	s->carry_len = 0;
}

static int sparse_stage_done(struct aml_burn_img *img)
{
	struct aml_burn_sparse *s = &img->sparse;
	u16 type;
	u32 csz, out_bytes;

	switch (s->stage) {
	case AML_SPARSE_FILE_HDR:
	{
		u16 hdr_sz = 0, chunk_sz = 0;

		if (s->carry_len < 28)
			return -EINVAL;
		{
			u32 magic;

			memcpy(&magic, s->carry, 4);
			if (magic != SPARSE_HEADER_MAGIC) {
				printf("aml_burn: not a sparse image (magic 0x%08x)\n",
				       magic);
				return -EINVAL;
			}
			memcpy(&s->blk_sz, s->carry + 12, 4);
			memcpy(&s->total_chunks, s->carry + 20, 4);
			memcpy(&hdr_sz, s->carry + 8, 2);
			memcpy(&chunk_sz, s->carry + 10, 2);
		}
		if (hdr_sz != 28 || chunk_sz != 12) {
			printf("aml_burn: unsupported sparse header sizes %u/%u\n",
			       hdr_sz, chunk_sz);
			return -EINVAL;
		}
		if (!s->blk_sz || (s->blk_sz & 3) ||
		    s->blk_sz < store_part.blksz ||
		    (s->blk_sz % store_part.blksz)) {
			printf("aml_burn: unsupported sparse block size %u\n",
			       s->blk_sz);
			return -EINVAL;
		}
		printf("aml_burn: sparse image, block size %u, %u chunks\n",
		       s->blk_sz, s->total_chunks);
		sparse_need_hdr(s, AML_SPARSE_CHUNK_HDR, 12);
		return 0;
	}

	case AML_SPARSE_CHUNK_HDR:
		memcpy(&type, s->carry, 2);
		memcpy(&csz, s->carry + 4, 4);
		out_bytes = csz * s->blk_sz;
		s->chunks_done++;

		if (s->out_off + out_bytes > store_part_bytes()) {
			printf("aml_burn: sparse chunk %u out of partition\n",
			       s->chunks_done);
			return -ENOSPC;
		}

		switch (type) {
		case CHUNK_TYPE_RAW:
			s->chunk_type = type;
			s->need = 0;
			s->carry_len = 0;
			if (!out_bytes) {
				sparse_need_hdr(s, AML_SPARSE_CHUNK_HDR, 12);
				return 0;
			}
			s->out_left = out_bytes;
			s->stage = AML_SPARSE_RAW;
			return 0;
		case CHUNK_TYPE_FILL:
			s->chunk_type = type;
			if (!out_bytes) {
				sparse_need_hdr(s, AML_SPARSE_CHUNK_HDR, 12);
				return 0;
			}
			s->out_left = out_bytes;
			sparse_need_hdr(s, AML_SPARSE_FILL_PAT, 4);
			return 0;
		case CHUNK_TYPE_DONT_CARE:
			s->out_off += out_bytes;
			sparse_need_hdr(s, AML_SPARSE_CHUNK_HDR, 12);
			return 0;
		case CHUNK_TYPE_CRC32:
			sparse_need_hdr(s, AML_SPARSE_CRC, 4);
			return 0;
		default:
			printf("aml_burn: unknown sparse chunk type 0x%04x\n",
			       type);
			return -EINVAL;
		}

	case AML_SPARSE_FILL_PAT:
	{
		int ret;

		memcpy(s->fill, s->carry, 4);
		ret = store_write_fill(s->out_off, s->fill, s->out_left);
		if (ret)
			return ret;
		s->out_off += s->out_left;
		s->out_left = 0;
		sparse_need_hdr(s, AML_SPARSE_CHUNK_HDR, 12);
		return 0;
	}

	case AML_SPARSE_CRC:
		/* CRC32 chunk: validated by the packer, not needed here */
		sparse_need_hdr(s, AML_SPARSE_CHUNK_HDR, 12);
		return 0;

	default:
		return -EINVAL;
	}
}

static int sparse_feed(struct aml_burn_img *img, const u8 *data, u32 len)
{
	struct aml_burn_sparse *s = &img->sparse;

	while (len) {
		if (s->need) {
			u32 n = min_t(u32, len, s->need - s->carry_len);
			int ret;

			memcpy(s->carry + s->carry_len, data, n);
			s->carry_len += n;
			data += n;
			len -= n;
			if (s->carry_len < s->need)
				break;
			ret = sparse_stage_done(img);
			if (ret)
				return ret;
			continue;
		}

		if (s->stage == AML_SPARSE_RAW && s->out_left) {
			u32 n = min_t(u32, len, s->out_left);
			int ret;

			ret = store_write(s->out_off, data, n);
			if (ret)
				return ret;
			s->out_off += n;
			s->out_left -= n;
			data += n;
			len -= n;
			if (!s->out_left)
				sparse_need_hdr(s, AML_SPARSE_CHUNK_HDR, 12);
			continue;
		}

		printf("aml_burn: sparse stream desync at 0x%llx\n",
		       (unsigned long long)img->wire);
		return -EINVAL;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Image handling							*/
/* ------------------------------------------------------------------ */

static int img_kind(const char *part, const char *type)
{
	if (!strcmp(type, "sparse"))
		return AML_IMG_SPARSE;
	if (!strcmp(part, "bootloader") || !strcmp(type, "bootloader"))
		return AML_IMG_BOOTLOADER;
	if (!strcmp(part, "_aml_dtb") || !strcmp(type, "dtb"))
		return AML_IMG_DTB;
	if (!strcmp(type, "normal"))
		return AML_IMG_NORMAL;

	return -EINVAL;
}

int aml_img_begin(const char *media, const char *part, const char *type,
		  u64 size, struct aml_burn_img *img)
{
	int kind, ret;

	memset(img, 0, sizeof(*img));

	if (strcmp(media, "store")) {
		printf("aml_burn: media \"%s\" not supported (only store)\n",
		       media);
		return -ENOTSUPP;
	}

	kind = img_kind(part, type);
	if (kind < 0) {
		printf("aml_burn: image type \"%s\" not supported\n", type);
		return -ENOTSUPP;
	}

	if (!size) {
		printf("aml_burn: bad image size 0x%llx\n",
		       (unsigned long long)size);
		return -EINVAL;
	}

	ret = aml_store_select(part);
	if (ret == -ENOENT && kind == AML_IMG_DTB && !strcmp(part, "_aml_dtb"))
		ret = aml_store_select("dtb");
	if (ret)
		return ret;

	/* Sparse input is not comparable with the partition size. */
	if (kind != AML_IMG_SPARSE && size > store_part_bytes()) {
		printf("aml_burn: image 0x%llx > partition %s (0x%llx B)\n",
		       (unsigned long long)size, part,
		       (unsigned long long)store_part_bytes());
		return -ENOSPC;
	}

	img->kind = kind;
	img->active = true;
	img->total = size;
	img->next_seq = 0;
	strlcpy(img->part, part, sizeof(img->part));
	strlcpy(img->type, type, sizeof(img->type));

	if (kind == AML_IMG_SPARSE)
		sparse_need_hdr(&img->sparse, AML_SPARSE_FILE_HDR, 28);

	printf("aml_burn: download %s/%s type %s size 0x%llx\n", media, part,
	       type, (unsigned long long)size);

	return 0;
}

int aml_img_write(struct aml_burn_img *img, const u8 *data, u32 len)
{
	int ret;

	if (!img->active)
		return -EPERM;
	if (img->wire + len > img->total) {
		printf("aml_burn: host sent 0x%x bytes past the end of %s\n",
		       len, img->part);
		return -EINVAL;
	}

	if (img->kind == AML_IMG_SPARSE) {
		ret = sparse_feed(img, data, len);
	} else {
		ret = store_write(img->media_off, data, len);
		if (!ret)
			img->media_off += len;
	}
	if (ret) {
		img->failed = true;
		return ret;
	}

	img->wire += len;
	if (img->wire == img->total) {
		img->complete = true;
		printf("aml_burn: %s complete (0x%llx bytes)\n", img->part,
		       (unsigned long long)img->wire);
	}

	return 0;
}

static int img_sha1(const struct aml_burn_img *img, u8 *digest)
{
	sha1_context ctx;
	u64 left = img->total;
	u64 off = 0;

	sha1_starts(&ctx);

	while (left) {
		u32 chunk = min_t(u64, left, AML_BURN_SHA1_CHUNK);
		int ret;

		ret = store_read(off, store_rd_bounce, chunk);
		if (ret)
			return ret;
		sha1_update(&ctx, store_rd_bounce, chunk);
		off += chunk;
		left -= chunk;
	}

	sha1_finish(&ctx, digest);

	return 0;
}

int aml_img_verify_sha1(const struct aml_burn_img *img, const char *hex)
{
	char sum[41];
	u8 digest[20];
	int i, ret;

	if (!img->active || !img->complete)
		return -EPERM;

	if (img->kind == AML_IMG_SPARSE) {
		printf("aml_burn: sha1 verification of sparse images is not supported\n");
		return -ENOTSUPP;
	}

	ret = img_sha1(img, digest);
	if (ret)
		return ret;

	for (i = 0; i < 20; i++)
		sprintf(sum + i * 2, "%02x", digest[i]);
	sum[40] = '\0';

	if (strcasecmp(sum, hex)) {
		printf("aml_burn: verify failed for %s: want %s, got %s\n",
		       img->part, hex, sum);
		return -EINVAL;
	}

	printf("aml_burn: verify ok for %s (%s)\n", img->part, sum);

	return 0;
}

/* ------------------------------------------------------------------ */
/* Readback (0x33)							*/
/* ------------------------------------------------------------------ */

/*
 * Copy `len' bytes of the device address space.  The AO secure word is read
 * through the secure APB, a normal access aborts the bus, so report it as
 * zero: pyamlboot only asks for it to decide whether secure boot is on.
 */
static void aml_mem_read(u64 addr, u8 *buf, u32 len)
{
	const u64 reg = AML_BURN_SECURE_REG;
	u64 end = addr + len;

	if (end <= reg || addr >= reg + 4) {
		memcpy(buf, (const void *)(uintptr_t)addr, len);
		return;
	}

	if (addr < reg) {
		u32 n = min_t(u32, (u32)(reg - addr), len);

		memcpy(buf, (const void *)(uintptr_t)addr, n);
		buf += n;
		len -= n;
		addr += n;
	}

	if (len) {
		u32 n = min_t(u32, 4 - (u32)(addr - reg), len);

		memset(buf, 0, n);
		buf += n;
		len -= n;
		addr += n;
	}

	if (len)
		memcpy(buf, (const void *)(uintptr_t)addr, len);
}

int aml_up_begin(const char *media, const char *part, const char *type,
		 u64 size, struct aml_burn_upload *up)
{
	int ret;

	memset(up, 0, sizeof(*up));

	if (!size) {
		printf("aml_burn: bad upload size 0x%llx\n",
		       (unsigned long long)size);
		return -EINVAL;
	}

	if (!strcmp(media, "mem")) {
		char *endp;
		u64 addr = simple_strtoull(part, &endp, 0);

		if (endp == part || *endp) {
			printf("aml_burn: upload mem needs a hex address\n");
			return -ENOTSUPP;
		}
		/*
		 * The tool only uses this to read the secure-boot register.  Do not
		 * expose arbitrary physical memory to the USB host.
		 */
		if (addr != AML_BURN_SECURE_REG || size > 16) {
			printf("aml_burn: upload mem 0x%llx is not allowed\n",
			       (unsigned long long)addr);
			return -ENOTSUPP;
		}
		up->kind = AML_UP_MEM;
		up->addr = addr;
	} else if (!strcmp(media, "store")) {
		if (!strcmp(type, "sparse")) {
			printf("aml_burn: sparse readback is not supported\n");
			return -ENOTSUPP;
		}
		ret = aml_store_select(part);
		if (ret)
			return ret;
		if (size > store_part_bytes()) {
			printf("aml_burn: upload 0x%llx > partition %s (0x%llx B)\n",
			       (unsigned long long)size, part,
			       (unsigned long long)store_part_bytes());
			return -ENOSPC;
		}
		up->kind = AML_UP_STORE;
	} else {
		printf("aml_burn: upload media \"%s\" not supported (mem, store)\n",
		       media);
		return -ENOTSUPP;
	}

	up->total = size;
	up->active = true;

	printf("aml_burn: upload %s/%s type %s size 0x%llx\n", media, part,
	       type, (unsigned long long)size);

	return 0;
}

int aml_up_read(struct aml_burn_upload *up, u64 off, u8 *buf, u32 len)
{
	if (!up->active)
		return -EPERM;

	if (off + len > up->total)
		return -EINVAL;

	if (up->kind == AML_UP_MEM) {
		/* Secure-boot register: a non-secure board reads as zeroes. */
		memset(buf, 0, len);
		return 0;
	}

	return store_read(off, buf, len);
}
