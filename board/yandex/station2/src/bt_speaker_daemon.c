/*
 * bt-speaker-daemon.c
 * Self-contained Bluetooth Classic BR/EDR Audio Sink daemon for AtriOS (Yandex Station Max).
 * Implements:
 *   - HCI controller setup (Yandex Station Max, Class 0x240414, SSP Just-Works, EIR, PISCAN)
 *   - SDP Server on L2CAP PSM 0x0001 (A2DP Sink, AVRCP Target/Controller, SPP)
 *   - AVDTP Signaling & Media Stream Sink on L2CAP PSM 0x0019 (25)
 *   - AVCTP / AVRCP Responder on L2CAP PSM 0x0017 (23)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>

#include <sbc/sbc.h>
#include <dlfcn.h>

typedef struct _snd_pcm snd_pcm_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;
#define SND_PCM_STREAM_PLAYBACK 0
#define SND_PCM_ACCESS_RW_INTERLEAVED 3
#define SND_PCM_FORMAT_S16_LE 2

#ifndef BT_SECURITY
#define BT_SECURITY 4
struct bt_security {
    uint8_t level;
    uint8_t key_size;
};
#define BT_SECURITY_SDP    0
#define BT_SECURITY_LOW    1
#define BT_SECURITY_MEDIUM 2
#define BT_SECURITY_HIGH   3
#endif

#ifndef SOL_BLUETOOTH
#define SOL_BLUETOOTH 274
#endif

static volatile sig_atomic_t g_running = 1;

static void handle_sig(int sig) {
    (void)sig;
    g_running = 0;
}

/* =========================================================================
 * HCI Helpers
 * ========================================================================= */

static void fmt_bdaddr(const bdaddr_t *ba, char *str) {
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);
}

static int hci_cmd_sync(int dd, uint16_t opcode, const void *param, uint8_t plen) {
    uint8_t buf[260];
    buf[0] = HCI_COMMAND_PKT;
    buf[1] = opcode & 0xff;
    buf[2] = (opcode >> 8) & 0xff;
    buf[3] = plen;
    if (plen > 0 && param)
        memcpy(buf + 4, param, plen);

    if (write(dd, buf, 4 + plen) < 0) {
        perror("hci write");
        return -1;
    }

    struct pollfd pfd = { .fd = dd, .events = POLLIN };
    for (int iter = 0; iter < 30; iter++) {
        int r = poll(&pfd, 1, 100);
        if (r <= 0) continue;
        uint8_t rsp[264];
        ssize_t n = read(dd, rsp, sizeof(rsp));
        if (n < 4 || rsp[0] != HCI_EVENT_PKT) continue;
        uint8_t evt = rsp[1];
        if (evt == EVT_CMD_COMPLETE && n >= 7) {
            uint16_t rop = rsp[4] | (rsp[5] << 8);
            if (rop == opcode) {
                return rsp[6];
            }
        } else if (evt == EVT_CMD_STATUS && n >= 7) {
            uint16_t rop = rsp[5] | (rsp[6] << 8);
            if (rop == opcode) {
                return rsp[3];
            }
        }
    }
    return -1;
}

static int ensure_hci0_configured(bool verbose) {
    int ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (ctl < 0) {
        if (verbose) perror("socket BTPROTO_HCI ctl");
        return -1;
    }
    if (ioctl(ctl, HCIDEVUP, 0) < 0 && errno != EALREADY && errno != 0) {
        if (verbose) perror("ioctl HCIDEVUP");
        close(ctl);
        return -1;
    }
    close(ctl);

    int dd = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (dd < 0) {
        if (verbose) perror("socket BTPROTO_HCI dd");
        return -1;
    }

    struct sockaddr_hci sa;
    memset(&sa, 0, sizeof(sa));
    sa.hci_family = AF_BLUETOOTH;
    sa.hci_dev = 0;
    if (bind(dd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        if (verbose) perror("bind hci0");
        close(dd);
        return -1;
    }

    struct hci_filter flt;
    hci_filter_clear(&flt);
    hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
    hci_filter_set_event(EVT_CMD_COMPLETE, &flt);
    hci_filter_set_event(EVT_CMD_STATUS, &flt);
    setsockopt(dd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt));

    /* 1. Write Local Name */
    uint8_t name[248];
    memset(name, 0, sizeof(name));
    strncpy((char *)name, "Yandex Station Max", sizeof(name) - 1);
    hci_cmd_sync(dd, 0x0c13, name, sizeof(name));

    /* 2. Write Class of Device: 0x240414 (Audio + Rendering, Loudspeaker) */
    uint8_t cod[3] = { 0x14, 0x04, 0x24 };
    hci_cmd_sync(dd, 0x0c24, cod, 3);

    /* 3. Write Simple Pairing Mode = 1 */
    uint8_t ssp = 1;
    hci_cmd_sync(dd, 0x0c56, &ssp, 1);

    /* 4. Write Authentication Enable = 0 (allow SDP/SSP cleanly) */
    uint8_t auth = 0;
    hci_cmd_sync(dd, 0x0c20, &auth, 1);

    /* 5. Write Inquiry Mode = 2 (EIR with RSSI) */
    uint8_t inq_mode = 2;
    hci_cmd_sync(dd, 0x0c45, &inq_mode, 1);

    /* 6. Write Extended Inquiry Response (Name + 16-bit Service UUIDs) */
    uint8_t eir[241];
    memset(eir, 0, sizeof(eir));
    eir[0] = 0; /* FEC not required */
    size_t pos = 1;
    const char *dev_name = "Yandex Station Max";
    uint8_t nlen = (uint8_t)strlen(dev_name);
    eir[pos++] = 1 + nlen;
    eir[pos++] = 0x09; /* Complete Local Name */
    memcpy(&eir[pos], dev_name, nlen);
    pos += nlen;
    /* Complete List of 16-bit Service Class UUIDs: 0x110B, 0x110C, 0x110E, 0x1101 */
    uint8_t uuids[] = {
        0x0b, 0x11, /* Audio Sink */
        0x0c, 0x11, /* AVRCP Target */
        0x0e, 0x11, /* AVRCP */
        0x01, 0x11  /* SPP */
    };
    eir[pos++] = 1 + sizeof(uuids);
    eir[pos++] = 0x03;
    memcpy(&eir[pos], uuids, sizeof(uuids));
    hci_cmd_sync(dd, 0x0c52, eir, sizeof(eir));

    /* 7. Default Link Policy Settings (0x080f): Role Switch + Hold + Sniff + Park */
    uint8_t lp[2] = { 0x0f, 0x00 };
    hci_cmd_sync(dd, 0x080f, lp, 2);

    /* 8. Write Page Timeout (0x0c18): 0x4000 slots (~10.24s) */
    uint8_t pto[2] = { 0x00, 0x40 };
    hci_cmd_sync(dd, 0x0c18, pto, 2);

    /* 9. Fast Interlaced Page Scan (interval=0x0200 / 320ms, window=0x0080 / 80ms) */
    uint8_t psa[4] = { 0x00, 0x02, 0x80, 0x00 };
    hci_cmd_sync(dd, 0x0c1c, psa, 4);
    uint8_t pst = 0x01;
    hci_cmd_sync(dd, 0x0c47, &pst, 1);

    /* 10. Fast Interlaced Inquiry Scan (interval=0x0400 / 640ms, window=0x0080 / 80ms) */
    uint8_t isa[4] = { 0x00, 0x04, 0x80, 0x00 };
    hci_cmd_sync(dd, 0x0c1e, isa, 4);
    uint8_t ist = 0x01;
    hci_cmd_sync(dd, 0x0c43, &ist, 1);

    /* 11. Write Scan Enable = 3 (Inquiry Scan + Page Scan) */
    uint8_t scan = 0x03;
    int st = hci_cmd_sync(dd, 0x0c1a, &scan, 1);

    /* 12. Disable connectable LE advertising if any stale adv is active so phones connect BR/EDR */
    uint8_t adv_off = 0x00;
    hci_cmd_sync(dd, 0x200a, &adv_off, 1);

    if (verbose) {
        printf("[bt-speaker] hci0 configured (Name='Yandex Station Max', CoD=0x240414, SSP=1, PISCAN=0x03, Interlaced=1, st=%d)\n", st);
        fflush(stdout);
    }
    close(dd);
    return 0;
}

/* =========================================================================
 * L2CAP Server Helper
 * ========================================================================= */

static int create_l2cap_server(uint16_t psm, uint16_t imtu, uint8_t sec_level) {
    int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (fd < 0) {
        perror("socket L2CAP");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct bt_security sec;
    memset(&sec, 0, sizeof(sec));
    sec.level = sec_level;
    sec.key_size = 0;
    if (setsockopt(fd, SOL_BLUETOOTH, BT_SECURITY, &sec, sizeof(sec)) < 0) {
        /* non-fatal on older kernels */
    }

    struct l2cap_options opts;
    socklen_t optlen = sizeof(opts);
    memset(&opts, 0, sizeof(opts));
    if (getsockopt(fd, SOL_L2CAP, L2CAP_OPTIONS, &opts, &optlen) == 0) {
        if (imtu > 0) {
            opts.imtu = imtu;
            opts.omtu = imtu;
            setsockopt(fd, SOL_L2CAP, L2CAP_OPTIONS, &opts, sizeof(opts));
        }
    }

    struct sockaddr_l2 loc;
    memset(&loc, 0, sizeof(loc));
    loc.l2_family = AF_BLUETOOTH;
    loc.l2_psm = htobs(psm);
    bacpy(&loc.l2_bdaddr, BDADDR_ANY);

    if (bind(fd, (struct sockaddr *)&loc, sizeof(loc)) < 0) {
        fprintf(stderr, "bind L2CAP PSM 0x%04x failed: %s\n", psm, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 8) < 0) {
        fprintf(stderr, "listen L2CAP PSM 0x%04x failed: %s\n", psm, strerror(errno));
        close(fd);
        return -1;
    }

    printf("[bt-speaker] Listening on L2CAP PSM 0x%04x (%u)\n", psm, psm);
    fflush(stdout);
    return fd;
}

/* =========================================================================
 * Spec-Compliant SDP Encoder & Server (L2CAP PSM 0x0001)
 * ========================================================================= */

typedef struct {
    uint8_t buf[2048];
    size_t len;
} sdp_buf_t;

static void sb_u8(sdp_buf_t *b, uint8_t v) {
    if (b->len < sizeof(b->buf)) b->buf[b->len++] = v;
}
static void sb_u16(sdp_buf_t *b, uint16_t v) {
    sb_u8(b, (v >> 8) & 0xff);
    sb_u8(b, v & 0xff);
}
static void sb_u32(sdp_buf_t *b, uint32_t v) {
    sb_u8(b, (v >> 24) & 0xff);
    sb_u8(b, (v >> 16) & 0xff);
    sb_u8(b, (v >> 8) & 0xff);
    sb_u8(b, v & 0xff);
}
static void sb_de_u8(sdp_buf_t *b, uint8_t v) {
    sb_u8(b, 0x08); /* UINT8 */
    sb_u8(b, v);
}
static void sb_de_u16(sdp_buf_t *b, uint16_t v) {
    sb_u8(b, 0x09); /* UINT16 */
    sb_u16(b, v);
}
static void sb_de_u32(sdp_buf_t *b, uint32_t v) {
    sb_u8(b, 0x0a); /* UINT32 */
    sb_u32(b, v);
}
static void sb_de_uuid16(sdp_buf_t *b, uint16_t u) {
    sb_u8(b, 0x19); /* UUID16 */
    sb_u16(b, u);
}
static void sb_de_str(sdp_buf_t *b, const char *s) {
    size_t n = strlen(s);
    sb_u8(b, 0x25); /* TEXT, 1-byte len */
    sb_u8(b, (uint8_t)n);
    for (size_t i = 0; i < n; i++) sb_u8(b, (uint8_t)s[i]);
}
static void sb_wrap_des(sdp_buf_t *out, const uint8_t *inner, size_t len) {
    if (len < 256) {
        sb_u8(out, 0x35);
        sb_u8(out, (uint8_t)len);
    } else {
        sb_u8(out, 0x36);
        sb_u16(out, (uint16_t)len);
    }
    if (out->len + len <= sizeof(out->buf)) {
        memcpy(out->buf + out->len, inner, len);
        out->len += len;
    }
}

typedef struct {
    uint16_t attr_id;
    uint8_t data[128];
    size_t len;
} sdp_attr_t;

typedef struct {
    uint32_t handle;
    uint16_t uuids[16];
    size_t uuid_cnt;
    sdp_attr_t attrs[10];
    size_t attr_cnt;
} sdp_record_def_t;

static sdp_record_def_t g_records[4];
static size_t g_record_cnt = 0;

static void add_attr(sdp_record_def_t *rec, uint16_t id, const sdp_buf_t *val) {
    if (rec->attr_cnt >= 10 || val->len > 128) return;
    sdp_attr_t *a = &rec->attrs[rec->attr_cnt++];
    a->attr_id = id;
    memcpy(a->data, val->buf, val->len);
    a->len = val->len;
}

static void init_sdp_records(void) {
    sdp_buf_t v, sub1, sub2, outer;
    memset(g_records, 0, sizeof(g_records));
    g_record_cnt = 0;

    /* --- Record 0: A2DP Audio Sink (0x110B) --- */
    {
        sdp_record_def_t *r = &g_records[g_record_cnt++];
        r->handle = 0x00010001;
        uint16_t u[] = { 0x110B, 0x0100, 0x0019, 0x110D, 0x1002 };
        r->uuid_cnt = 5; memcpy(r->uuids, u, sizeof(u));

        /* 0x0000: ServiceRecordHandle */
        v.len = 0; sb_de_u32(&v, r->handle); add_attr(r, 0x0000, &v);
        /* 0x0001: ServiceClassIDList = { AudioSink 0x110B } */
        sub1.len = 0; sb_de_uuid16(&sub1, 0x110B);
        outer.len = 0; sb_wrap_des(&outer, sub1.buf, sub1.len); add_attr(r, 0x0001, &outer);
        /* 0x0004: ProtocolDescriptorList = { {L2CAP, 0x0019}, {AVDTP, 0x0103} } */
        sub1.len = 0; sb_de_uuid16(&sub1, 0x0100); sb_de_u16(&sub1, 0x0019);
        sub2.len = 0; sb_de_uuid16(&sub2, 0x0019); sb_de_u16(&sub2, 0x0103);
        v.len = 0; sb_wrap_des(&v, sub1.buf, sub1.len); sb_wrap_des(&v, sub2.buf, sub2.len);
        outer.len = 0; sb_wrap_des(&outer, v.buf, v.len); add_attr(r, 0x0004, &outer);
        /* 0x0005: BrowseGroupList = { PublicBrowseRoot 0x1002 } */
        sub1.len = 0; sb_de_uuid16(&sub1, 0x1002);
        outer.len = 0; sb_wrap_des(&outer, sub1.buf, sub1.len); add_attr(r, 0x0005, &outer);
        /* 0x0009: BluetoothProfileDescriptorList = { {0x110D, 0x0103} } */
        sub1.len = 0; sb_de_uuid16(&sub1, 0x110D); sb_de_u16(&sub1, 0x0103);
        v.len = 0; sb_wrap_des(&v, sub1.buf, sub1.len);
        outer.len = 0; sb_wrap_des(&outer, v.buf, v.len); add_attr(r, 0x0009, &outer);
        /* 0x0100: ServiceName */
        v.len = 0; sb_de_str(&v, "Audio Sink"); add_attr(r, 0x0100, &v);
        /* 0x0311: SupportedFeatures (Player/Speaker = 1) */
        v.len = 0; sb_de_u16(&v, 0x0001); add_attr(r, 0x0311, &v);
    }

    /* --- Record 1: AVRCP Target (0x110C) --- */
    {
        sdp_record_def_t *r = &g_records[g_record_cnt++];
        r->handle = 0x00010002;
        uint16_t u[] = { 0x110C, 0x0100, 0x0017, 0x110E, 0x1002 };
        r->uuid_cnt = 5; memcpy(r->uuids, u, sizeof(u));

        v.len = 0; sb_de_u32(&v, r->handle); add_attr(r, 0x0000, &v);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x110C);
        outer.len = 0; sb_wrap_des(&outer, sub1.buf, sub1.len); add_attr(r, 0x0001, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x0100); sb_de_u16(&sub1, 0x0017);
        sub2.len = 0; sb_de_uuid16(&sub2, 0x0017); sb_de_u16(&sub2, 0x0104);
        v.len = 0; sb_wrap_des(&v, sub1.buf, sub1.len); sb_wrap_des(&v, sub2.buf, sub2.len);
        outer.len = 0; sb_wrap_des(&outer, v.buf, v.len); add_attr(r, 0x0004, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x1002);
        outer.len = 0; sb_wrap_des(&outer, sub1.buf, sub1.len); add_attr(r, 0x0005, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x110E); sb_de_u16(&sub1, 0x0105);
        v.len = 0; sb_wrap_des(&v, sub1.buf, sub1.len);
        outer.len = 0; sb_wrap_des(&outer, v.buf, v.len); add_attr(r, 0x0009, &outer);
        v.len = 0; sb_de_u16(&v, 0x0002); add_attr(r, 0x0311, &v);
    }

    /* --- Record 2: AVRCP Controller (0x110E / 0x110F) --- */
    {
        sdp_record_def_t *r = &g_records[g_record_cnt++];
        r->handle = 0x00010003;
        uint16_t u[] = { 0x110E, 0x110F, 0x0100, 0x0017, 0x1002 };
        r->uuid_cnt = 5; memcpy(r->uuids, u, sizeof(u));

        v.len = 0; sb_de_u32(&v, r->handle); add_attr(r, 0x0000, &v);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x110E); sb_de_uuid16(&sub1, 0x110F);
        outer.len = 0; sb_wrap_des(&outer, sub1.buf, sub1.len); add_attr(r, 0x0001, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x0100); sb_de_u16(&sub1, 0x0017);
        sub2.len = 0; sb_de_uuid16(&sub2, 0x0017); sb_de_u16(&sub2, 0x0104);
        v.len = 0; sb_wrap_des(&v, sub1.buf, sub1.len); sb_wrap_des(&v, sub2.buf, sub2.len);
        outer.len = 0; sb_wrap_des(&outer, v.buf, v.len); add_attr(r, 0x0004, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x1002);
        outer.len = 0; sb_wrap_des(&outer, sub1.buf, sub1.len); add_attr(r, 0x0005, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x110E); sb_de_u16(&sub1, 0x0105);
        v.len = 0; sb_wrap_des(&v, sub1.buf, sub1.len);
        outer.len = 0; sb_wrap_des(&outer, v.buf, v.len); add_attr(r, 0x0009, &outer);
        v.len = 0; sb_de_u16(&v, 0x0001); add_attr(r, 0x0311, &v);
    }

    /* --- Record 3: Serial Port Profile (0x1101) --- */
    {
        sdp_record_def_t *r = &g_records[g_record_cnt++];
        r->handle = 0x00010004;
        uint16_t u[] = { 0x1101, 0x0100, 0x0003, 0x1002 };
        r->uuid_cnt = 4; memcpy(r->uuids, u, sizeof(u));

        v.len = 0; sb_de_u32(&v, r->handle); add_attr(r, 0x0000, &v);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x1101);
        outer.len = 0; sb_wrap_des(&outer, sub1.buf, sub1.len); add_attr(r, 0x0001, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x0100);
        sub2.len = 0; sb_de_uuid16(&sub2, 0x0003); sb_de_u8(&sub2, 1);
        v.len = 0; sb_wrap_des(&v, sub1.buf, sub1.len); sb_wrap_des(&v, sub2.buf, sub2.len);
        outer.len = 0; sb_wrap_des(&outer, v.buf, v.len); add_attr(r, 0x0004, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x1002);
        outer.len = 0; sb_wrap_des(&outer, sub1.buf, sub1.len); add_attr(r, 0x0005, &outer);
        sub1.len = 0; sb_de_uuid16(&sub1, 0x1101); sb_de_u16(&sub1, 0x0102);
        v.len = 0; sb_wrap_des(&v, sub1.buf, sub1.len);
        outer.len = 0; sb_wrap_des(&outer, v.buf, v.len); add_attr(r, 0x0009, &outer);
        v.len = 0; sb_de_str(&v, "AtriOS Control"); add_attr(r, 0x0100, &v);
    }
}

/* Parse a Data Element header at ptr, advancing *off to payload and returning payload_len + type */
static int parse_de_hdr(const uint8_t *p, size_t max_len, size_t *off, uint8_t *type_out, size_t *dlen_out) {
    if (*off >= max_len) return -1;
    uint8_t b = p[(*off)++];
    uint8_t t = b >> 3;
    uint8_t sd = b & 0x07;
    size_t dlen = 0;
    if (t == 0) {
        dlen = 0;
    } else if (sd == 0) dlen = 1;
    else if (sd == 1) dlen = 2;
    else if (sd == 2) dlen = 4;
    else if (sd == 3) dlen = 8;
    else if (sd == 4) dlen = 16;
    else if (sd == 5) {
        if (*off + 1 > max_len) return -1;
        dlen = p[(*off)++];
    } else if (sd == 6) {
        if (*off + 2 > max_len) return -1;
        dlen = (p[*off] << 8) | p[*off + 1];
        *off += 2;
    } else if (sd == 7) {
        if (*off + 4 > max_len) return -1;
        dlen = ((size_t)p[*off] << 24) | ((size_t)p[*off+1] << 16) | ((size_t)p[*off+2] << 8) | p[*off+3];
        *off += 4;
    }
    if (*off + dlen > max_len) return -1;
    *type_out = t;
    *dlen_out = dlen;
    return 0;
}

static bool rec_has_uuid(const sdp_record_def_t *r, uint16_t u) {
    for (size_t i = 0; i < r->uuid_cnt; i++) {
        if (r->uuids[i] == u) return true;
    }
    return false;
}

static int parse_uuid_list(const uint8_t *p, size_t max_len, size_t *off, uint16_t *uuids, size_t max_u, size_t *cnt_out, bool *has_nonmatch_128) {
    uint8_t t = 0; size_t seq_len = 0;
    if (parse_de_hdr(p, max_len, off, &t, &seq_len) < 0 || t != 6) return -1;
    size_t end = *off + seq_len;
    *cnt_out = 0;
    *has_nonmatch_128 = false;
    static const uint8_t bt_base_tail[12] = { 0x00,0x00,0x10,0x00,0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb };
    while (*off < end) {
        uint8_t ut = 0; size_t ulen = 0;
        if (parse_de_hdr(p, end, off, &ut, &ulen) < 0) return -1;
        if (ut == 3) {
            uint16_t u16 = 0;
            if (ulen == 2) {
                u16 = (p[*off] << 8) | p[*off + 1];
            } else if (ulen == 4) {
                u16 = (p[*off + 2] << 8) | p[*off + 3];
            } else if (ulen == 16) {
                if (memcmp(p + *off + 4, bt_base_tail, 12) == 0) {
                    u16 = (p[*off + 2] << 8) | p[*off + 3];
                } else {
                    *has_nonmatch_128 = true;
                }
            }
            if (u16 != 0 && *cnt_out < max_u) {
                uuids[(*cnt_out)++] = u16;
            }
        }
        *off += ulen;
    }
    return 0;
}

typedef struct { uint16_t start, end; } attr_range_t;

static int parse_attr_ranges(const uint8_t *p, size_t max_len, size_t *off, attr_range_t *ranges, size_t max_r, size_t *cnt_out) {
    uint8_t t = 0; size_t seq_len = 0;
    if (parse_de_hdr(p, max_len, off, &t, &seq_len) < 0 || t != 6) return -1;
    size_t end = *off + seq_len;
    *cnt_out = 0;
    while (*off < end) {
        uint8_t at = 0; size_t alen = 0;
        if (parse_de_hdr(p, end, off, &at, &alen) < 0) return -1;
        if (at == 1 && *cnt_out < max_r) {
            if (alen == 2) {
                uint16_t id = (p[*off] << 8) | p[*off + 1];
                ranges[(*cnt_out)++] = (attr_range_t){ id, id };
            } else if (alen == 4) {
                uint16_t s = (p[*off] << 8) | p[*off + 1];
                uint16_t e = (p[*off + 2] << 8) | p[*off + 3];
                ranges[(*cnt_out)++] = (attr_range_t){ s, e };
            }
        }
        *off += alen;
    }
    return 0;
}

static bool attr_in_ranges(uint16_t id, const attr_range_t *r, size_t cnt) {
    for (size_t i = 0; i < cnt; i++) {
        if (id >= r[i].start && id <= r[i].end) return true;
    }
    return false;
}

static void build_record_attr_des(const sdp_record_def_t *rec, const attr_range_t *ranges, size_t rcnt, sdp_buf_t *out) {
    sdp_buf_t inner;
    inner.len = 0;
    for (size_t i = 0; i < rec->attr_cnt; i++) {
        if (attr_in_ranges(rec->attrs[i].attr_id, ranges, rcnt)) {
            sb_de_u16(&inner, rec->attrs[i].attr_id);
            if (inner.len + rec->attrs[i].len <= sizeof(inner.buf)) {
                memcpy(inner.buf + inner.len, rec->attrs[i].data, rec->attrs[i].len);
                inner.len += rec->attrs[i].len;
            }
        }
    }
    out->len = 0;
    if (inner.len > 0) {
        sb_wrap_des(out, inner.buf, inner.len);
    }
}

/* =========================================================================
 * Client Connection State Table
 * ========================================================================= */

typedef enum {
    CONN_SDP = 1,
    CONN_AVDTP_SIGNAL,
    CONN_AVDTP_MEDIA,
    CONN_AVCTP
} conn_kind_t;

#define MAX_CLIENTS 32

typedef struct {
    bool active;
    int fd;
    conn_kind_t kind;
    bdaddr_t peer;
    char peer_str[18];
    /* SDP continuation buffer */
    uint8_t sdp_cache[2048];
    size_t sdp_cache_len;
    /* AVDTP state */
    bool avdtp_open_sent;
    uint8_t avdtp_cfg[64];
    size_t avdtp_cfg_len;
    uint64_t media_pkts;
    /* AVRCP volume notification state */
    bool vol_notify_registered;
    uint8_t vol_notify_tlabel;
    uint8_t vol_notify_subunit;
    uint8_t vol_notify_pid[2];
    int last_sent_avrcp_vol;
} client_t;

static client_t g_clients[MAX_CLIENTS];

static client_t *alloc_client(int fd, conn_kind_t kind, const bdaddr_t *peer) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].active) {
            memset(&g_clients[i], 0, sizeof(client_t));
            g_clients[i].active = true;
            g_clients[i].fd = fd;
            g_clients[i].kind = kind;
            bacpy(&g_clients[i].peer, peer);
            fmt_bdaddr(peer, g_clients[i].peer_str);
            return &g_clients[i];
        }
    }
    return NULL;
}

static client_t *find_avdtp_signal_by_peer(const bdaddr_t *peer) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && g_clients[i].kind == CONN_AVDTP_SIGNAL &&
            bacmp(&g_clients[i].peer, peer) == 0) {
            return &g_clients[i];
        }
    }
    return NULL;
}

static void audio_close(void);

static void close_client(client_t *c) {
    if (!c || !c->active) return;
    printf("[bt-speaker] Disconnected %s (kind=%d, media_pkts=%llu)\n",
           c->peer_str, (int)c->kind, (unsigned long long)c->media_pkts);
    fflush(stdout);
    if (c->kind == CONN_AVDTP_MEDIA) {
        audio_close();
    }
    close(c->fd);
    c->active = false;
    c->fd = -1;
}

/* =========================================================================
 * SDP Handler
 * ========================================================================= */

static void handle_sdp_pdu(client_t *c, const uint8_t *pkt, size_t len) {
    if (len < 5) return;
    uint8_t pdu_id = pkt[0];
    uint16_t tid = (pkt[1] << 8) | pkt[2];
    uint16_t plen = (pkt[3] << 8) | pkt[4];
    if ((size_t)(5 + plen) > len) return;
    const uint8_t *p = pkt + 5;
    size_t off = 0;

    uint8_t rsp[1024];
    rsp[1] = (tid >> 8) & 0xff;
    rsp[2] = tid & 0xff;

    if (pdu_id == 0x02) { /* SDP_SVC_SEARCH_REQ */
        uint16_t uuids[16]; size_t ucnt = 0; bool bad128 = false;
        if (parse_uuid_list(p, plen, &off, uuids, 16, &ucnt, &bad128) < 0 || off + 3 > plen) return;
        uint16_t max_rec = (p[off] << 8) | p[off + 1]; off += 2;
        (void)p[off]; /* cont_len */

        uint32_t handles[4];
        uint16_t hcnt = 0;
        if (!bad128 && ucnt > 0) {
            for (size_t i = 0; i < g_record_cnt && hcnt < max_rec; i++) {
                bool match = true;
                for (size_t j = 0; j < ucnt; j++) {
                    if (!rec_has_uuid(&g_records[i], uuids[j])) { match = false; break; }
                }
                if (match) handles[hcnt++] = g_records[i].handle;
            }
        }
        rsp[0] = 0x03; /* SDP_SVC_SEARCH_RSP */
        size_t out = 5;
        rsp[out++] = (hcnt >> 8) & 0xff; rsp[out++] = hcnt & 0xff; /* TotalServiceRecordCount */
        rsp[out++] = (hcnt >> 8) & 0xff; rsp[out++] = hcnt & 0xff; /* CurrentServiceRecordCount */
        for (uint16_t i = 0; i < hcnt; i++) {
            rsp[out++] = (handles[i] >> 24) & 0xff;
            rsp[out++] = (handles[i] >> 16) & 0xff;
            rsp[out++] = (handles[i] >> 8) & 0xff;
            rsp[out++] = handles[i] & 0xff;
        }
        rsp[out++] = 0x00; /* ContinuationState len = 0 */
        uint16_t rplen = (uint16_t)(out - 5);
        rsp[3] = (rplen >> 8) & 0xff; rsp[4] = rplen & 0xff;
        send(c->fd, rsp, out, 0);
        return;
    }

    if (pdu_id == 0x04 || pdu_id == 0x06) {
        uint16_t max_bytes = 500;
        sdp_buf_t full;
        full.len = 0;

        if (pdu_id == 0x04) { /* SDP_SVC_ATTR_REQ */
            if (plen < 7) return;
            uint32_t handle = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
            max_bytes = (p[4] << 8) | p[5];
            off = 6;
            attr_range_t ranges[16]; size_t rcnt = 0;
            if (parse_attr_ranges(p, plen, &off, ranges, 16, &rcnt) < 0 || off + 1 > plen) return;
            for (size_t i = 0; i < g_record_cnt; i++) {
                if (g_records[i].handle == handle) {
                    build_record_attr_des(&g_records[i], ranges, rcnt, &full);
                    break;
                }
            }
            if (full.len == 0) {
                sdp_buf_t empty_des; empty_des.len = 0;
                sb_wrap_des(&full, empty_des.buf, 0);
            }
        } else { /* 0x06 SDP_SVC_SEARCH_ATTR_REQ */
            uint16_t uuids[16]; size_t ucnt = 0; bool bad128 = false;
            if (parse_uuid_list(p, plen, &off, uuids, 16, &ucnt, &bad128) < 0 || off + 2 > plen) return;
            max_bytes = (p[off] << 8) | p[off + 1]; off += 2;
            attr_range_t ranges[16]; size_t rcnt = 0;
            if (parse_attr_ranges(p, plen, &off, ranges, 16, &rcnt) < 0 || off + 1 > plen) return;

            sdp_buf_t list_inner; list_inner.len = 0;
            if (!bad128 && ucnt > 0) {
                for (size_t i = 0; i < g_record_cnt; i++) {
                    bool match = true;
                    for (size_t j = 0; j < ucnt; j++) {
                        if (!rec_has_uuid(&g_records[i], uuids[j])) { match = false; break; }
                    }
                    if (match) {
                        sdp_buf_t rdes;
                        build_record_attr_des(&g_records[i], ranges, rcnt, &rdes);
                        if (rdes.len > 0 && list_inner.len + rdes.len <= sizeof(list_inner.buf)) {
                            memcpy(list_inner.buf + list_inner.len, rdes.buf, rdes.len);
                            list_inner.len += rdes.len;
                        }
                    }
                }
            }
            sb_wrap_des(&full, list_inner.buf, list_inner.len);
        }

        uint8_t cont_len = p[off];
        uint16_t start_off = 0;
        if (cont_len == 2 && off + 3 <= plen) {
            start_off = (p[off + 1] << 8) | p[off + 2];
        } else {
            memcpy(c->sdp_cache, full.buf, full.len);
            c->sdp_cache_len = full.len;
        }

        if (start_off > c->sdp_cache_len) start_off = (uint16_t)c->sdp_cache_len;
        uint16_t chunk = max_bytes;
        if (chunk > 560) chunk = 560;
        if (chunk < 32) chunk = 32;
        size_t remain = c->sdp_cache_len - start_off;
        if (chunk > remain) chunk = (uint16_t)remain;

        rsp[0] = (pdu_id == 0x04) ? 0x05 : 0x07;
        size_t out = 5;
        rsp[out++] = (chunk >> 8) & 0xff;
        rsp[out++] = chunk & 0xff;
        memcpy(rsp + out, c->sdp_cache + start_off, chunk);
        out += chunk;
        size_t next_off = start_off + chunk;
        if (next_off < c->sdp_cache_len) {
            rsp[out++] = 0x02;
            rsp[out++] = (next_off >> 8) & 0xff;
            rsp[out++] = next_off & 0xff;
        } else {
            rsp[out++] = 0x00;
        }
        uint16_t rplen = (uint16_t)(out - 5);
        rsp[3] = (rplen >> 8) & 0xff;
        rsp[4] = rplen & 0xff;
        send(c->fd, rsp, out, 0);
    }
}

/* =========================================================================
 * AVDTP Handler (L2CAP PSM 0x0019)
 * ========================================================================= */

static void handle_avdtp_signal(client_t *c, const uint8_t *pkt, size_t len) {
    if (len < 2) return;
    uint8_t tx_label = (pkt[0] >> 4) & 0x0f;
    uint8_t pkt_type = (pkt[0] >> 2) & 0x03;
    uint8_t msg_type = pkt[0] & 0x03;
    if (pkt_type != 0 || msg_type != 0) return; /* only handle SINGLE COMMAND */

    uint8_t sig_id = pkt[1] & 0x3f;
    uint8_t rsp[256];
    size_t rlen = 2;
    rsp[0] = (uint8_t)((tx_label << 4) | 0x02); /* SINGLE + RESPONSE ACCEPT */
    rsp[1] = sig_id;

    printf("[bt-speaker] AVDTP CMD from %s: sig=0x%02x len=%zu\n", c->peer_str, sig_id, len);
    fflush(stdout);

    switch (sig_id) {
    case 0x01: /* AVDTP_DISCOVER */
        /* ACP SEID=1 (0x04), MediaType=0 Audio | TSEP=1 SNK (0x08) */
        rsp[rlen++] = 0x04;
        rsp[rlen++] = 0x08;
        break;

    case 0x02: /* AVDTP_GET_CAPABILITIES */
    case 0x0C: /* AVDTP_GET_ALL_CAPABILITIES */
        if (len >= 3 && (pkt[2] >> 2) != 1) {
            rsp[0] = (uint8_t)((tx_label << 4) | 0x03); /* REJECT */
            rsp[rlen++] = 0x12; /* BAD_ACP_SEID */
            break;
        }
        /* Media Transport (cat 0x01, losc 0) */
        rsp[rlen++] = 0x01;
        rsp[rlen++] = 0x00;
        /* Media Codec SBC (cat 0x07, losc 6): Audio=0x00, SBC=0x00, all freqs/modes, min=2 max=53 */
        rsp[rlen++] = 0x07;
        rsp[rlen++] = 0x06;
        rsp[rlen++] = 0x00;
        rsp[rlen++] = 0x00;
        rsp[rlen++] = 0xff;
        rsp[rlen++] = 0xff;
        rsp[rlen++] = 0x02;
        rsp[rlen++] = 0x35;
        break;

    case 0x03: /* AVDTP_SET_CONFIGURATION */
        if (len > 4) {
            size_t clen = len - 4;
            if (clen > sizeof(c->avdtp_cfg)) clen = sizeof(c->avdtp_cfg);
            memcpy(c->avdtp_cfg, pkt + 4, clen);
            c->avdtp_cfg_len = clen;
        }
        break;

    case 0x04: /* AVDTP_GET_CONFIGURATION */
        if (c->avdtp_cfg_len > 0) {
            memcpy(rsp + rlen, c->avdtp_cfg, c->avdtp_cfg_len);
            rlen += c->avdtp_cfg_len;
        }
        break;

    case 0x05: /* AVDTP_RECONFIGURE */
        if (len > 3) {
            size_t clen = len - 3;
            if (clen > sizeof(c->avdtp_cfg)) clen = sizeof(c->avdtp_cfg);
            memcpy(c->avdtp_cfg, pkt + 3, clen);
            c->avdtp_cfg_len = clen;
        }
        break;

    case 0x06: /* AVDTP_OPEN */
        c->avdtp_open_sent = true;
        break;

    case 0x07: /* AVDTP_START */
    case 0x08: /* AVDTP_CLOSE */
    case 0x09: /* AVDTP_SUSPEND */
    case 0x0A: /* AVDTP_ABORT */
    case 0x0D: /* AVDTP_DELAYREPORT */
        if (sig_id == 0x08 || sig_id == 0x0A) {
            c->avdtp_open_sent = false;
        }
        if (sig_id == 0x08 || sig_id == 0x09 || sig_id == 0x0A) {
            audio_close();
        }
        break;

    default:
        rsp[0] = (uint8_t)((tx_label << 4) | 0x03); /* REJECT */
        rsp[rlen++] = 0x19; /* NOT_SUPPORTED_COMMAND */
        break;
    }

    send(c->fd, rsp, rlen, 0);
}

/* =========================================================================
 * SBC Decoder & ALSA Playback (A2DP Media Sink via dlopen)
 * ========================================================================= */

static void *g_sbc_dl = NULL;
static void *g_alsa_dl = NULL;
static int (*p_sbc_init)(sbc_t *, unsigned long) = NULL;
static ssize_t (*p_sbc_decode)(sbc_t *, const void *, size_t, void *, size_t, size_t *) = NULL;
static void (*p_sbc_finish)(sbc_t *) = NULL;

static int (*p_snd_pcm_open)(snd_pcm_t **, const char *, int, int) = NULL;
static int (*p_snd_pcm_close)(snd_pcm_t *) = NULL;
static int (*p_snd_pcm_set_params)(snd_pcm_t *, int, int, unsigned int, unsigned int, int, unsigned int) = NULL;
static snd_pcm_sframes_t (*p_snd_pcm_writei)(snd_pcm_t *, const void *, snd_pcm_uframes_t) = NULL;
static int (*p_snd_pcm_recover)(snd_pcm_t *, int, int) = NULL;
static int (*p_snd_pcm_drain)(snd_pcm_t *) = NULL;
static int (*p_snd_pcm_drop)(snd_pcm_t *) = NULL;
static const char *(*p_snd_strerror)(int) = NULL;

static sbc_t g_sbc;
static bool g_sbc_init = false;
static snd_pcm_t *g_pcm = NULL;
static unsigned int g_pcm_rate = 0;
static unsigned int g_pcm_channels = 0;

static bool audio_load_libs(void) {
    if (p_sbc_decode && p_snd_pcm_writei) return true;
    if (!g_sbc_dl) g_sbc_dl = dlopen("libsbc.so.1", RTLD_LAZY);
    if (!g_alsa_dl) g_alsa_dl = dlopen("libasound.so.2", RTLD_LAZY);
    if (!g_sbc_dl || !g_alsa_dl) {
        fprintf(stderr, "[bt-speaker] dlopen failed: sbc=%p alsa=%p (%s)\n",
                g_sbc_dl, g_alsa_dl, dlerror());
        return false;
    }
    p_sbc_init = dlsym(g_sbc_dl, "sbc_init");
    p_sbc_decode = dlsym(g_sbc_dl, "sbc_decode");
    p_sbc_finish = dlsym(g_sbc_dl, "sbc_finish");
    p_snd_pcm_open = dlsym(g_alsa_dl, "snd_pcm_open");
    p_snd_pcm_close = dlsym(g_alsa_dl, "snd_pcm_close");
    p_snd_pcm_set_params = dlsym(g_alsa_dl, "snd_pcm_set_params");
    p_snd_pcm_writei = dlsym(g_alsa_dl, "snd_pcm_writei");
    p_snd_pcm_recover = dlsym(g_alsa_dl, "snd_pcm_recover");
    p_snd_pcm_drain = dlsym(g_alsa_dl, "snd_pcm_drain");
    p_snd_pcm_drop = dlsym(g_alsa_dl, "snd_pcm_drop");
    p_snd_strerror = dlsym(g_alsa_dl, "snd_strerror");
    return (p_sbc_init && p_sbc_decode && p_sbc_finish &&
            p_snd_pcm_open && p_snd_pcm_close && p_snd_pcm_set_params &&
            p_snd_pcm_writei && p_snd_pcm_recover && p_snd_pcm_drain &&
            p_snd_pcm_drop && p_snd_strerror);
}

static void audio_close(void) {
    if (g_pcm && p_snd_pcm_drop && p_snd_pcm_close) {
        p_snd_pcm_drop(g_pcm);
        p_snd_pcm_close(g_pcm);
        g_pcm = NULL;
        g_pcm_rate = 0;
        g_pcm_channels = 0;
    }
    if (g_sbc_init && p_sbc_finish) {
        p_sbc_finish(&g_sbc);
        g_sbc_init = false;
    }
}

static bool audio_ensure_pcm(unsigned int rate, unsigned int channels) {
    if (!audio_load_libs()) return false;
    if (g_pcm && g_pcm_rate == rate && g_pcm_channels == channels) return true;
    if (g_pcm) {
        p_snd_pcm_drain(g_pcm);
        p_snd_pcm_close(g_pcm);
        g_pcm = NULL;
    }
    const char *devs[] = { "default", "plughw:0,0" };
    int err = -1;
    for (size_t i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        err = p_snd_pcm_open(&g_pcm, devs[i], SND_PCM_STREAM_PLAYBACK, 0);
        if (err >= 0) {
            err = p_snd_pcm_set_params(g_pcm, SND_PCM_FORMAT_S16_LE,
                                       SND_PCM_ACCESS_RW_INTERLEAVED,
                                       channels, rate, 1, 100000);
            if (err >= 0) {
                g_pcm_rate = rate;
                g_pcm_channels = channels;
                printf("[bt-speaker] ALSA opened '%s' (%u Hz, %u ch)\n",
                       devs[i], rate, channels);
                fflush(stdout);
                return true;
            }
            p_snd_pcm_close(g_pcm);
            g_pcm = NULL;
        }
    }
    fprintf(stderr, "[bt-speaker] ALSA open/setup failed: %s\n", p_snd_strerror(err));
    return false;
}

static void handle_avdtp_media(client_t *c, const uint8_t *pkt, size_t len) {
    c->media_pkts++;
    if (len < 14 || !audio_load_libs()) return;

    uint8_t v = (pkt[0] >> 6) & 0x03;
    if (v != 2) return; /* Not RTP v2 */
    uint8_t cc = pkt[0] & 0x0f;
    bool ext = (pkt[0] & 0x10) != 0;

    size_t off = 12 + (size_t)cc * 4;
    if (ext) {
        if (off + 4 > len) return;
        uint16_t ext_len = (uint16_t)((pkt[off + 2] << 8) | pkt[off + 3]);
        off += 4 + (size_t)ext_len * 4;
    }
    if (off + 1 >= len) return;

    /* Skip 1-byte SBC RTP media payload header */
    off += 1;

    if (!g_sbc_init) {
        if (p_sbc_init(&g_sbc, 0) < 0) return;
        g_sbc.endian = SBC_LE;
        g_sbc_init = true;
    }

    uint8_t pcm_buf[8192];
    while (off < len) {
        size_t written = 0;
        ssize_t consumed = p_sbc_decode(&g_sbc, pkt + off, len - off,
                                        pcm_buf, sizeof(pcm_buf), &written);
        if (consumed <= 0) break;
        off += (size_t)consumed;

        if (written > 0) {
            unsigned int rate = (g_sbc.frequency == 0) ? 16000 :
                                (g_sbc.frequency == 1) ? 32000 :
                                (g_sbc.frequency == 2) ? 44100 : 48000;
            unsigned int ch = (g_sbc.mode == 0) ? 1 : 2;
            if (!audio_ensure_pcm(rate, ch)) break;

            snd_pcm_uframes_t total_frames = (snd_pcm_uframes_t)(written / (ch * 2));
            size_t byte_off = 0;
            while (total_frames > 0) {
                snd_pcm_sframes_t n = p_snd_pcm_writei(g_pcm, pcm_buf + byte_off, total_frames);
                if (n < 0) {
                    n = p_snd_pcm_recover(g_pcm, (int)n, 1);
                    if (n < 0) break;
                    continue;
                }
                total_frames -= (snd_pcm_uframes_t)n;
                byte_off += (size_t)n * ch * 2;
            }
        }
    }
}

/* =========================================================================
 * AVCTP / AVRCP Handler (L2CAP PSM 0x0017) & Bidirectional Volume Sync
 * ========================================================================= */

#define VOLUME_STATE_FILE "/var/lib/atrios/volume"

static int g_vol_pct = 60;
static struct timespec g_vol_mtime = {0, 0};

static int pct_to_avrcp(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (pct * 127 + 50) / 100;
}

static int avrcp_to_pct(uint8_t avrcp_vol) {
    int v = avrcp_vol & 0x7f;
    return (v * 100 + 63) / 127;
}

static bool read_volume_file(int *out_pct, struct timespec *out_mtime) {
    struct stat st;
    if (stat(VOLUME_STATE_FILE, &st) < 0) return false;
    FILE *f = fopen(VOLUME_STATE_FILE, "r");
    if (!f) return false;
    int val = -1;
    int ok = (fscanf(f, "%d", &val) == 1);
    fclose(f);
    if (!ok) return false;
    if (val < 0) val = 0;
    if (val > 100) val = 100;
    if (out_pct) *out_pct = val;
    if (out_mtime) *out_mtime = st.st_mtim;
    return true;
}

static void write_volume_file(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const char *tmp = VOLUME_STATE_FILE ".tmp";
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "%d\n", pct);
    fclose(f);
    if (rename(tmp, VOLUME_STATE_FILE) == 0) {
        struct stat st;
        if (stat(VOLUME_STATE_FILE, &st) == 0) {
            g_vol_mtime = st.st_mtim;
        }
    }
}

static void send_avrcp_volume_changed(client_t *c, int pct) {
    if (!c || !c->active || c->kind != CONN_AVCTP || !c->vol_notify_registered) return;
    int av = pct_to_avrcp(pct);
    if (av == c->last_sent_avrcp_vol) return;
    uint8_t rsp[15];
    rsp[0] = (uint8_t)((c->vol_notify_tlabel << 4) | 0x02); /* Single, Response */
    rsp[1] = c->vol_notify_pid[0];
    rsp[2] = c->vol_notify_pid[1];
    rsp[3] = 0x0d; /* CHANGED */
    rsp[4] = c->vol_notify_subunit;
    rsp[5] = 0x00; /* VENDOR DEPENDENT */
    rsp[6] = 0x00; rsp[7] = 0x19; rsp[8] = 0x58;
    rsp[9] = 0x31; /* RegisterNotification */
    rsp[10] = 0x00;
    rsp[11] = 0x00; rsp[12] = 0x02;
    rsp[13] = 0x0d; /* EVENT_VOLUME_CHANGED */
    rsp[14] = (uint8_t)(av & 0x7f);
    if (send(c->fd, rsp, sizeof(rsp), 0) == (ssize_t)sizeof(rsp)) {
        c->vol_notify_registered = false;
        c->last_sent_avrcp_vol = av;
        printf("[bt-speaker] AVRCP -> %s: CHANGED volume=%d%% (avrcp=%d)\n",
               c->peer_str, pct, av);
        fflush(stdout);
    }
}

static void handle_avctp(client_t *c, const uint8_t *pkt, size_t len) {
    if (len < 3) return;
    /* Only reply to commands (C/R bit 1 == 0) in SINGLE packets (bits 3:2 == 0) */
    uint8_t hdr0 = pkt[0];
    if ((hdr0 & 0x0e) != 0) return;

    uint8_t rsp[256];
    if (len > sizeof(rsp)) return;
    memcpy(rsp, pkt, len);
    rsp[0] = hdr0 | 0x02; /* C/R = Response (1) */

    if (len >= 6) {
        uint8_t ctype = pkt[3] & 0x0f;
        uint8_t opcode = pkt[5];
        bool is_bt_sig = (len >= 10 && opcode == 0x00 &&
                          pkt[6] == 0x00 && pkt[7] == 0x19 && pkt[8] == 0x58);

        if (ctype == 0x01) { /* STATUS */
            rsp[3] = 0x0c; /* IMPLEMENTED / STABLE */
            if (opcode == 0x30 && len >= 11) { /* UNIT_INFO */
                rsp[6] = 0x07; rsp[7] = (0x09 << 3) | 0x00;
                rsp[8] = 0x00; rsp[9] = 0x19; rsp[10] = 0x58;
            } else if (opcode == 0x31 && len >= 11) { /* SUBUNIT_INFO */
                rsp[6] = 0x07; rsp[7] = (0x09 << 3) | 0x00;
                rsp[8] = 0xff; rsp[9] = 0xff; rsp[10] = 0xff;
            } else if (is_bt_sig && len >= 14 && pkt[9] == 0x10) { /* GetCapabilities */
                if (pkt[13] == 0x02) { /* Company IDs */
                    rsp[11] = 0x00; rsp[12] = 0x05;
                    rsp[13] = 0x02; rsp[14] = 0x01;
                    rsp[15] = 0x00; rsp[16] = 0x19; rsp[17] = 0x58;
                    send(c->fd, rsp, 18, 0);
                    return;
                } else if (pkt[13] == 0x03) { /* Events Supported */
                    rsp[11] = 0x00; rsp[12] = 0x04; /* param len = 4 */
                    rsp[13] = 0x03; rsp[14] = 0x02; /* 2 events */
                    rsp[15] = 0x01; rsp[16] = 0x0d; /* PlaybackStatus, VolumeChanged */
                    send(c->fd, rsp, 17, 0);
                    return;
                }
            }
        } else if (ctype == 0x00) { /* CONTROL */
            rsp[3] = 0x09; /* ACCEPTED */
            if (is_bt_sig && len >= 14 && pkt[9] == 0x50) { /* SetAbsoluteVolume */
                uint8_t av = pkt[13] & 0x7f;
                int pct = avrcp_to_pct(av);
                g_vol_pct = pct;
                c->last_sent_avrcp_vol = av;
                write_volume_file(pct);
                rsp[11] = 0x00; rsp[12] = 0x01;
                rsp[13] = av;
                printf("[bt-speaker] AVRCP <- %s: SetAbsoluteVolume avrcp=%d -> %d%%\n",
                       c->peer_str, av, pct);
                fflush(stdout);
                send(c->fd, rsp, 14, 0);
                return;
            } else if (opcode == 0x7c && len >= 7) { /* PASS THROUGH */
                uint8_t op_id = pkt[6] & 0x7f;
                bool released = (pkt[6] & 0x80) != 0;
                if (!released && (op_id == 0x41 || op_id == 0x42)) {
                    read_volume_file(&g_vol_pct, &g_vol_mtime);
                    g_vol_pct += (op_id == 0x41) ? 5 : -5;
                    if (g_vol_pct < 0) g_vol_pct = 0;
                    if (g_vol_pct > 100) g_vol_pct = 100;
                    write_volume_file(g_vol_pct);
                }
            }
        } else if (ctype == 0x03) { /* NOTIFY */
            if (is_bt_sig && len >= 14 && pkt[9] == 0x31) { /* RegisterNotification */
                uint8_t event_id = pkt[13];
                if (event_id == 0x0d) { /* EVENT_VOLUME_CHANGED */
                    read_volume_file(&g_vol_pct, &g_vol_mtime);
                    int av = pct_to_avrcp(g_vol_pct);
                    c->vol_notify_registered = true;
                    c->vol_notify_tlabel = (uint8_t)((hdr0 >> 4) & 0x0f);
                    c->vol_notify_subunit = pkt[4];
                    c->vol_notify_pid[0] = pkt[1];
                    c->vol_notify_pid[1] = pkt[2];
                    c->last_sent_avrcp_vol = av;
                    rsp[3] = 0x0f; /* INTERIM */
                    rsp[11] = 0x00; rsp[12] = 0x02;
                    rsp[13] = 0x0d;
                    rsp[14] = (uint8_t)(av & 0x7f);
                    printf("[bt-speaker] AVRCP <- %s: RegisterNotification(VOL) -> INTERIM avrcp=%d (%d%%)\n",
                           c->peer_str, av, g_vol_pct);
                    fflush(stdout);
                    send(c->fd, rsp, 15, 0);
                    return;
                } else if (event_id == 0x01) { /* EVENT_PLAYBACK_STATUS_CHANGED */
                    rsp[3] = 0x0f; /* INTERIM */
                    rsp[11] = 0x00; rsp[12] = 0x02;
                    rsp[13] = 0x01;
                    rsp[14] = 0x01; /* PLAYING */
                    send(c->fd, rsp, 15, 0);
                    return;
                } else {
                    rsp[3] = 0x08; /* NOT IMPLEMENTED */
                }
            } else {
                rsp[3] = 0x0f; /* INTERIM */
            }
        }
    }
    send(c->fd, rsp, len, 0);
}

/* =========================================================================
 * HCI Raw Event Monitor
 * ========================================================================= */

static int open_hci_monitor(void) {
    int fd = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (fd < 0) return -1;

    struct sockaddr_hci sa;
    memset(&sa, 0, sizeof(sa));
    sa.hci_family = AF_BLUETOOTH;
    sa.hci_dev = 0;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(fd);
        return -1;
    }

    struct hci_filter flt;
    hci_filter_clear(&flt);
    hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
    hci_filter_all_events(&flt);
    setsockopt(fd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt));
    return fd;
}

static void handle_hci_event(int fd) {
    uint8_t buf[264];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 3 || buf[0] != HCI_EVENT_PKT) return;

    uint8_t evt = buf[1];
    uint8_t plen = buf[2];
    const uint8_t *p = buf + 3;
    if ((ssize_t)(3 + plen) > n) return;

    char bstr[18];
    switch (evt) {
    case 0x04: /* Connection Request */
        if (plen >= 10) {
            fmt_bdaddr((const bdaddr_t *)p, bstr);
            printf("[bt-hci] EVT_CONN_REQUEST peer=%s link_type=0x%02x cod=%02x%02x%02x\n",
                   bstr, p[9], p[8], p[7], p[6]);
            fflush(stdout);
        }
        break;
    case 0x03: /* Connection Complete */
        if (plen >= 11) {
            fmt_bdaddr((const bdaddr_t *)(p + 3), bstr);
            uint16_t handle = p[1] | (p[2] << 8);
            printf("[bt-hci] EVT_CONN_COMPLETE status=0x%02x handle=0x%04x peer=%s type=0x%02x enc=%u\n",
                   p[0], handle, bstr, p[9], p[10]);
            fflush(stdout);
        }
        break;
    case 0x05: /* Disconnection Complete */
        if (plen >= 4) {
            uint16_t handle = p[1] | (p[2] << 8);
            printf("[bt-hci] EVT_DISCONN_COMPLETE status=0x%02x handle=0x%04x reason=0x%02x\n",
                   p[0], handle, p[3]);
            fflush(stdout);
        }
        break;
    case 0x06: /* Authentication Complete */
        if (plen >= 3) {
            uint16_t handle = p[1] | (p[2] << 8);
            printf("[bt-hci] EVT_AUTH_COMPLETE status=0x%02x handle=0x%04x\n", p[0], handle);
            fflush(stdout);
        }
        break;
    case 0x08: /* Encryption Change */
        if (plen >= 4) {
            uint16_t handle = p[1] | (p[2] << 8);
            printf("[bt-hci] EVT_ENCRYPT_CHANGE status=0x%02x handle=0x%04x enc=%u\n", p[0], handle, p[3]);
            fflush(stdout);
        }
        break;
    case 0x16: /* PIN Code Request */
    case 0x17: /* Link Key Request */
    case 0x18: /* Link Key Notification */
    case 0x31: /* IO Capability Request */
    case 0x33: /* User Confirmation Request */
    case 0x36: /* Simple Pairing Complete */
        if (plen >= 6) {
            if (evt == 0x36 && plen >= 7) {
                fmt_bdaddr((const bdaddr_t *)(p + 1), bstr);
                printf("[bt-hci] EVT_SSP_COMPLETE status=0x%02x peer=%s\n", p[0], bstr);
            } else {
                fmt_bdaddr((const bdaddr_t *)p, bstr);
                printf("[bt-hci] EVT=0x%02x peer=%s\n", evt, bstr);
            }
            fflush(stdout);
        }
        break;
    default:
        break;
    }
}

/* =========================================================================
 * Main Event Loop
 * ========================================================================= */

int main(void) {
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);
    signal(SIGPIPE, SIG_IGN);

    init_sdp_records();
    read_volume_file(&g_vol_pct, &g_vol_mtime);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        g_clients[i].active = false;
        g_clients[i].fd = -1;
    }

    while (g_running && ensure_hci0_configured(true) < 0) {
        fprintf(stderr, "[bt-speaker] Waiting for hci0...\n");
        sleep(2);
    }
    if (!g_running) return 0;

    int fd_sdp = create_l2cap_server(0x0001, 672, BT_SECURITY_SDP);
    int fd_avdtp = create_l2cap_server(0x0019, 895, BT_SECURITY_LOW);
    int fd_avctp = create_l2cap_server(0x0017, 672, BT_SECURITY_LOW);
    int fd_hci = open_hci_monitor();

    if (fd_sdp < 0 || fd_avdtp < 0) {
        fprintf(stderr, "[bt-speaker] Failed to bind core L2CAP servers!\n");
        return 1;
    }

    time_t last_refresh = time(NULL);

    while (g_running) {
        struct pollfd pfds[4 + MAX_CLIENTS];
        int pmap[4 + MAX_CLIENTS];
        nfds_t nfds = 0;

        pfds[nfds] = (struct pollfd){ .fd = fd_sdp, .events = POLLIN };
        pmap[nfds++] = -1;
        pfds[nfds] = (struct pollfd){ .fd = fd_avdtp, .events = POLLIN };
        pmap[nfds++] = -2;
        if (fd_avctp >= 0) {
            pfds[nfds] = (struct pollfd){ .fd = fd_avctp, .events = POLLIN };
            pmap[nfds++] = -3;
        }
        if (fd_hci >= 0) {
            pfds[nfds] = (struct pollfd){ .fd = fd_hci, .events = POLLIN };
            pmap[nfds++] = -4;
        }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].active) {
                pfds[nfds] = (struct pollfd){ .fd = g_clients[i].fd, .events = POLLIN };
                pmap[nfds++] = i;
            }
        }

        int ret = poll(pfds, nfds, 30);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        struct stat vst;
        if (stat(VOLUME_STATE_FILE, &vst) == 0 &&
            (vst.st_mtim.tv_sec != g_vol_mtime.tv_sec ||
             vst.st_mtim.tv_nsec != g_vol_mtime.tv_nsec)) {
            int new_pct = g_vol_pct;
            if (read_volume_file(&new_pct, &g_vol_mtime) && new_pct != g_vol_pct) {
                g_vol_pct = new_pct;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (g_clients[i].active && g_clients[i].kind == CONN_AVCTP) {
                        send_avrcp_volume_changed(&g_clients[i], g_vol_pct);
                    }
                }
            }
        }

        for (nfds_t i = 0; i < nfds; i++) {
            if (!(pfds[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
            int id = pmap[i];
            if (id == -4) {
                handle_hci_event(fd_hci);
            } else if (id < 0) {
                struct sockaddr_l2 rem;
                socklen_t rlen = sizeof(rem);
                int cfd = accept(pfds[i].fd, (struct sockaddr *)&rem, &rlen);
                if (cfd >= 0) {
                    char bstr[18];
                    fmt_bdaddr(&rem.l2_bdaddr, bstr);
                    conn_kind_t kind = CONN_SDP;
                    if (id == -1) kind = CONN_SDP;
                    else if (id == -3) kind = CONN_AVCTP;
                    else if (id == -2) {
                        client_t *sig = find_avdtp_signal_by_peer(&rem.l2_bdaddr);
                        if (sig && sig->avdtp_open_sent) {
                            kind = CONN_AVDTP_MEDIA;
                            sig->avdtp_open_sent = false;
                        } else {
                            kind = CONN_AVDTP_SIGNAL;
                        }
                    }
                    client_t *nc = alloc_client(cfd, kind, &rem.l2_bdaddr);
                    if (!nc) {
                        close(cfd);
                    } else {
                        printf("[bt-speaker] Accepted %s (kind=%d)\n", bstr, (int)kind);
                        fflush(stdout);
                    }
                }
            } else {
                client_t *c = &g_clients[id];
                uint8_t buf[2048];
                ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    close_client(c);
                    continue;
                }
                if (c->kind == CONN_SDP) handle_sdp_pdu(c, buf, (size_t)n);
                else if (c->kind == CONN_AVDTP_SIGNAL) handle_avdtp_signal(c, buf, (size_t)n);
                else if (c->kind == CONN_AVDTP_MEDIA) handle_avdtp_media(c, buf, (size_t)n);
                else if (c->kind == CONN_AVCTP) handle_avctp(c, buf, (size_t)n);
            }
        }

        time_t now = time(NULL);
        if (now - last_refresh >= 15) {
            last_refresh = now;
            /* Ensure scan stays enabled without disturbing active connections */
            int dd = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
            if (dd >= 0) {
                struct sockaddr_hci sa = { .hci_family = AF_BLUETOOTH, .hci_dev = 0 };
                if (bind(dd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
                    struct hci_filter flt;
                    hci_filter_clear(&flt);
                    hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
                    hci_filter_set_event(EVT_CMD_COMPLETE, &flt);
                    setsockopt(dd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt));
                    uint8_t scan = 0x03;
                    hci_cmd_sync(dd, 0x0c1a, &scan, 1);
                }
                close(dd);
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) close_client(&g_clients[i]);
    if (fd_sdp >= 0) close(fd_sdp);
    if (fd_avdtp >= 0) close(fd_avdtp);
    if (fd_avctp >= 0) close(fd_avctp);
    if (fd_hci >= 0) close(fd_hci);
    return 0;
}
