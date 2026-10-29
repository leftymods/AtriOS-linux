#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Convert the RTL8822E PHY/RF tables from the vendor driver sources
# (rtl8822cs-driver, tree rtl8822cs/, GPL-2.0) into the format used by
# drivers/net/wireless/realtek/rtw88.
#
# Usage:
#   ./rtw88-8822e-tables.py --src /tmp/wifi-drop \
#           --c drivers/net/wireless/realtek/rtw88/rtw8822e_table.c \
#           --h drivers/net/wireless/realtek/rtw88/rtw8822e_table.h
#
# The vendor files needed in --src:
#   halhwimg8822e_bb.c      -> array_mp_8822e_agc_tab, _phy_reg, _phy_reg_pg
#   halhwimg8822e_rf.c      -> array_mp_8822e_radioa, _radiob, _txpwr_lmt[_type5]
#   halrf_rfk_init_8822e.h  -> array_mp_8822e_cal_init
#
# What the conversion does
# ------------------------
# The vendor PHY tables are flat u32 arrays:
#
#   { header0, idx0, header1, idx1, ... , addr, data, addr, data, ... }
#
# The leading "headline" pairs (whose upper nibble is 0xf) select one of
# several table variants at runtime by (cut_version, rfe_type); the chosen
# header's low 28 bits become cfg_target.  Inside the table body three
# pseudo registers encode control flow:
#
#   0x8xxxxxxx  IF       (store cfg_para = value & 0x0fffffff)
#   0x9xxxxxxx  ELSE_IF  (store cfg_para)
#   0xA0000000  ELSE
#   0xB0000000  ENDIF
#   0x4xxxxxxx  CHK      (if !find_target: matched = cfg_para == cfg_target)
#
# rtw88's built-in rtw_parse_tbl_phy_cond() cannot reproduce that: its
# `cond` matching is a plain AND/OR of (cut, pkg, intf, rfe) fields and it
# has no notion of a headline.  Converting the headline into concrete
# cut/rfe conditions (what upstream rtw88 did for 8822C) would bake in a
# guess about the target chip's cut/rfe revision.  Instead we keep the
# vendor arrays verbatim (including the headline) and ship
# rtw8822e_parse_tbl_phy_cond(), a small parser that implements the vendor
# algorithm 1:1.  That way the tables are byte-for-byte the vendor ones.
#
# txpwr_lmt tables are converted to struct rtw_txpwr_lmt_cfg_pair.  The
# vendor table has a per-antenna (ntx: 1T/2T) dimension that rtw88 does not
# model; upstream rtw88 8822C tables keep the 1T values, so we do the same
# and drop the 2T rows.  Vendor regulation enum values are mapped to
# rtw88's enum rtw_regulatory_domains.

import argparse
import os
import re
import sys

# ---------------------------------------------------------------- parsing

COMMENT_RE = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)


def read_array_body(path, name):
    """Return the text between the outermost braces of array `name`."""
    with open(path, 'r', errors='replace') as f:
        src = COMMENT_RE.sub('', f.read())
    m = re.search(r'\b' + re.escape(name) + r'\s*\[[^\]]*\]\s*=\s*\{', src)
    if not m:
        raise SystemExit('array %s not found in %s' % (name, path))
    start = m.end() - 1
    depth = 0
    i = start
    while i < len(src):
        c = src[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    return src[start + 1:i]


def read_array(path, name):
    """Return the list of C initializer '{...}' groups inside array `name`."""
    body = read_array_body(path, name)
    return [g.strip() for g in re.findall(r'\{([^{}]*)\}', body)]


def parse_phy(path, name):
    """Flat u32 pair array -> [u32, ...]."""
    body = read_array_body(path, name)
    vals = [int(v, 0) for v in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', body)]
    if len(vals) % 2:
        raise SystemExit('%s: odd u32 count %d' % (name, len(vals)))
    return vals


def parse_u32_rows(path, name, width):
    body = read_array_body(path, name)
    vals = [int(v, 0) for v in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', body)]
    if len(vals) % width:
        raise SystemExit('%s: %d values is not a multiple of %d'
                         % (name, len(vals), width))
    return [vals[i:i + width] for i in range(0, len(vals), width)]


# vendor enum odm_pw_lmt_regulation_type -> rtw88 enum rtw_regulatory_domains
REGD = {
    'PW_LMT_REGU_FCC': 0,
    'PW_LMT_REGU_MKK': 1,
    'PW_LMT_REGU_ETSI': 2,
    'PW_LMT_REGU_WW13': None,	# no rtw88 equivalent; not used by 8822E
    'PW_LMT_REGU_IC': 3,
    'PW_LMT_REGU_KCC': 4,
    'PW_LMT_REGU_ACMA': 5,
    'PW_LMT_REGU_CHILE': 6,
    'PW_LMT_REGU_UKRAINE': 7,
    'PW_LMT_REGU_MEXICO': 8,
    'PW_LMT_REGU_CN': 9,
    'PW_LMT_REGU_QATAR': 10,
    'PW_LMT_REGU_UK': 11,
}
BAND = {'PW_LMT_BAND_2_4G': 0, 'PW_LMT_BAND_5G': 1}
BW = {'PW_LMT_BW_20M': 0, 'PW_LMT_BW_40M': 1, 'PW_LMT_BW_80M': 2}
RS = {'PW_LMT_RS_CCK': 0, 'PW_LMT_RS_OFDM': 1,
      'PW_LMT_RS_HT': 2, 'PW_LMT_RS_VHT': 3}
PH = {'PW_LMT_PH_1T': 0, 'PW_LMT_PH_2T': 1}


def parse_txpwr(path, name):
    rows = parse_u32_rows_txpwr(path, name)
    out = []
    dropped = 0
    for reg, band, bw, rs, ntx, ch, val in rows:
        if ntx != 'PW_LMT_PH_1T':
            dropped += 1
            continue
        out.append((REGD[reg], BAND[band], BW[bw], RS[rs], int(ch),
                    int(val)))
    return out, dropped


def parse_u32_rows_txpwr(path, name):
    groups = read_array(path, name)
    rows = []
    for g in groups:
        fields = [f.strip() for f in g.split(',') if f.strip()]
        if len(fields) != 7:
            raise SystemExit('%s: entry %r has %d fields'
                             % (name, g, len(fields)))
        rows.append(fields)
    return rows


# ---------------------------------------------------------------- emitting

def emit_phy(out, cname, pairs, comment):
    out.write('/* %s: %d u32 words, %d (addr, data) pairs */\n'
              % (comment, len(pairs), len(pairs) // 2))
    out.write('static const u32 %s[] = {\n' % cname)
    for i in range(0, len(pairs), 2):
        out.write('\t0x%08X, 0x%08X,\n' % (pairs[i], pairs[i + 1]))
    out.write('};\n\n')


def emit_bb_pg(out, cname, rows):
    out.write('/* %d entries */\n' % len(rows))
    out.write('static const struct rtw_phy_pg_cfg_pair %s[] = {\n' % cname)
    for r in rows:
        out.write('\t{ %d, %d, %d, 0x%08x, 0x%08x, 0x%08x, },\n'
                  % tuple(r))
    out.write('};\n\n')


def emit_txpwr(out, cname, rows):
    out.write('/* %d entries (1T only) */\n' % len(rows))
    out.write('static const struct rtw_txpwr_lmt_cfg_pair %s[] = {\n'
              % cname)
    for r in rows:
        out.write('\t{ %d, %d, %d, %d, %d, %d, },\n' % r)
    out.write('};\n\n')


HEADER = '''// SPDX-License-Identifier: GPL-2.0
/*
 * RTL8822E PHY/RF tables.
 *
 * Converted from the GPL-2.0 vendor driver published as
 * github.com/Cohen0415/rtl8822cs-driver (tree rtl8822cs/):
 *   halhwimg8822e_bb.c, halhwimg8822e_rf.c, halrf_rfk_init_8822e.h
 *
 * The arrays are kept verbatim, including the vendor "headline" preamble
 * and the 0x8/0x9/0xa/0xb/0x4 control markers.  They are executed by
 * rtw8822e_parse_tbl_phy_cond() below, which is a 1:1 port of the vendor
 * halbb_sel_headline() + odm_read_and_config_mp_8822e_*() logic.
 *
 * Generated by tools/rtw88-8822e-tables.py -- do not edit.
 */

#include "main.h"
#include "phy.h"
#include "rtw8822c.h"
#include "rtw8822e_table.h"

#define PARA_IF\t\t0x8
#define PARA_ELSE_IF\t0x9
#define PARA_ELSE\t0xa
#define PARA_END\t0xb
#define PARA_CHK\t0x4

#define CUT_DONT_CARE\t0xf
#define RFE_DONT_CARE\t0xff
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True)
    ap.add_argument('--c', required=True)
    ap.add_argument('--h', required=True)
    args = ap.parse_args()

    bb_c = os.path.join(args.src, 'halhwimg8822e_bb.c')
    rf_c = os.path.join(args.src, 'halhwimg8822e_rf.c')
    rfk_h = os.path.join(args.src, 'halrf_rfk_init_8822e.h')

    agc = parse_phy(bb_c, 'array_mp_8822e_agc_tab')
    bb = parse_phy(bb_c, 'array_mp_8822e_phy_reg')
    bb_pg = parse_u32_rows(bb_c, 'array_mp_8822e_phy_reg_pg', 6)
    rf_a = parse_phy(rf_c, 'array_mp_8822e_radioa')
    rf_b = parse_phy(rf_c, 'array_mp_8822e_radiob')
    cal = parse_phy(rfk_h, 'array_mp_8822e_cal_init')
    lmt0, drop0 = parse_txpwr(rf_c, 'array_mp_8822e_txpwr_lmt')
    lmt5, drop5 = parse_txpwr(rf_c, 'array_mp_8822e_txpwr_lmt_type5')

    with open(args.c, 'w') as out:
        out.write(HEADER)
        emit_phy(out, 'rtw8822e_agc', agc, 'array_mp_8822e_agc_tab')
        emit_phy(out, 'rtw8822e_bb', bb, 'array_mp_8822e_phy_reg')
        emit_bb_pg(out, 'rtw8822e_bb_pg_type0', bb_pg)
        emit_phy(out, 'rtw8822e_rf_a', rf_a, 'array_mp_8822e_radioa')
        emit_phy(out, 'rtw8822e_rf_b', rf_b, 'array_mp_8822e_radiob')
        emit_phy(out, 'rtw8822e_array_mp_cal_init', cal,
                 'array_mp_8822e_cal_init')
        emit_txpwr(out, 'rtw8822e_txpwr_lmt_type0', lmt0)
        emit_txpwr(out, 'rtw8822e_txpwr_lmt_type5', lmt5)
        out.write('''/* ----------------------------------------------------------------
 * Parser: verbatim port of the vendor phy table engine
 * ---------------------------------------------------------------- */

static void rtw8822e_parse_tbl_phy_cond(struct rtw_dev *rtwdev,
\t\t\t\t\tconst struct rtw_table *tbl)
{
\tconst u32 *array = tbl->data;
\tu32 array_len = tbl->size;
\tu32 h_size = 0, h_idx = 0, cfg_target = 0, cfg_para = 0;
\tu32 cut_max = 0, cut, rfe, i, v1, v2, written = 0;
\tbool is_matched = true, find_target = false, found = false;

\tcut = rtwdev->hal.cut_version ? rtwdev->hal.cut_version : 15;
\trfe = rtwdev->efuse.rfe_option;

\twhile ((h_size + 1) < array_len) {
\t\tif ((array[h_size] >> 28) != 0xf)
\t\t\tbreak;
\t\th_size += 2;
\t}

\t/* case 1: {cut: match, rfe: match} */
\tfor (i = 0; i < h_size; i += 2) {
\t\tif ((array[i] & 0x0f0000ff) == ((cut & 0xf) << 24 | rfe)) {
\t\t\th_idx = i >> 1;
\t\t\tgoto have_header;
\t\t}
\t}
\t/* case 2: {cut: don't care, rfe: match} */
\tfor (i = 0; i < h_size; i += 2) {
\t\tif ((array[i] & 0x0f0000ff) == (CUT_DONT_CARE << 24 | rfe)) {
\t\t\th_idx = i >> 1;
\t\t\tgoto have_header;
\t\t}
\t}
\t/* case 3: {rfe: match, cut: greatest in table} */
\tfor (i = 0; i < h_size; i += 2) {
\t\tif ((array[i] & 0xff) != rfe)
\t\t\tcontinue;
\t\tfound = true;
\t\tif ((array[i] & 0x0f000000) >> 24 >= cut_max) {
\t\t\tcut_max = (array[i] & 0x0f000000) >> 24;
\t\t\th_idx = i >> 1;
\t\t}
\t}
\tif (found)
\t\tgoto have_header;
\t/* case 4: {rfe: don't care, cut: greatest in table} */
\tcut_max = 0;
\tfor (i = 0; i < h_size; i += 2) {
\t\tif ((array[i] & 0xff) != RFE_DONT_CARE)
\t\t\tcontinue;
\t\tfound = true;
\t\tif ((array[i] & 0x0f000000) >> 24 >= cut_max) {
\t\t\tcut_max = (array[i] & 0x0f000000) >> 24;
\t\t\th_idx = i >> 1;
\t\t}
\t}
\tif (!found)
\t\tgoto out;

have_header:
\tif (h_size)
\t\tcfg_target = array[h_idx << 1] & 0x0fffffff;

\tfor (i = h_size; (i + 1) < array_len; i += 2) {
\t\tv1 = array[i];
\t\tv2 = array[i + 1];

\t\tswitch (v1 >> 28) {
\t\tcase PARA_IF:
\t\tcase PARA_ELSE_IF:
\t\t\tcfg_para = v1 & 0x0fffffff;
\t\t\tbreak;
\t\tcase PARA_ELSE:
\t\t\tis_matched = false;
\t\t\tif (!find_target)
\t\t\t\tgoto out;
\t\t\tbreak;
\t\tcase PARA_END:
\t\t\tis_matched = true;
\t\t\tfind_target = false;
\t\t\tbreak;
\t\tcase PARA_CHK:
\t\t\tif (find_target) {
\t\t\t\tis_matched = false;
\t\t\t\tbreak;
\t\t\t}
\t\t\tis_matched = cfg_para == cfg_target;
\t\t\tfind_target = is_matched;
\t\t\tbreak;
\t\tdefault:
\t\t\tif (is_matched) {
\t\t\t\ttbl->do_cfg(rtwdev, tbl, v1, v2);
\t\t\t\twritten++;
\t\t\t}
\t\t}
\t}

out:
\tif (!written)
\t\trtw_warn(rtwdev,
\t\t\t "rtw8822e: no phy entries applied (cut %u rfe 0x%02x, %u words)\\n",
\t\t\t cut, rfe, array_len);
}

#define RTW8822E_DECL_PHY(name, cfg, path)\t\t\\
const struct rtw_table name ## _tbl = {\t\t\\
\t.data = name,\t\t\t\t\\
\t.size = ARRAY_SIZE(name),\t\t\\
\t.parse = rtw8822e_parse_tbl_phy_cond,\t\\
\t.do_cfg = cfg,\t\t\t\t\\
\t.rf_path = path,\t\t\t\\
}

RTW8822E_DECL_PHY(rtw8822e_agc, rtw_phy_cfg_agc, 0);
RTW8822E_DECL_PHY(rtw8822e_bb, rtw_phy_cfg_bb, 0);
RTW_DECL_TABLE_BB_PG(rtw8822e_bb_pg_type0);
RTW8822E_DECL_PHY(rtw8822e_rf_a, rtw_phy_cfg_rf, RF_PATH_A);
RTW8822E_DECL_PHY(rtw8822e_rf_b, rtw_phy_cfg_rf, RF_PATH_B);
RTW_DECL_TABLE_PHY_COND(rtw8822e_array_mp_cal_init, rtw_phy_cfg_bb);
RTW_DECL_TABLE_TXPWR_LMT(rtw8822e_txpwr_lmt_type0);
RTW_DECL_TABLE_TXPWR_LMT(rtw8822e_txpwr_lmt_type5);
''')

    with open(args.h, 'w') as out:
        out.write('''/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RTL8822E table declarations.  The data is generated by
 * tools/rtw88-8822e-tables.py from the vendor driver.
 */

#ifndef __RTW8822E_TABLE_H__
#define __RTW8822E_TABLE_H__

extern const struct rtw_table rtw8822e_agc_tbl;
extern const struct rtw_table rtw8822e_bb_tbl;
extern const struct rtw_table rtw8822e_bb_pg_type0_tbl;
extern const struct rtw_table rtw8822e_rf_a_tbl;
extern const struct rtw_table rtw8822e_rf_b_tbl;
extern const struct rtw_table rtw8822e_array_mp_cal_init_tbl;
extern const struct rtw_table rtw8822e_txpwr_lmt_type0_tbl;
extern const struct rtw_table rtw8822e_txpwr_lmt_type5_tbl;

#endif
''')

    print('agc        %5d u32' % len(agc))
    print('bb         %5d u32' % len(bb))
    print('bb_pg      %5d rows' % len(bb_pg))
    print('rf_a       %5d u32' % len(rf_a))
    print('rf_b       %5d u32' % len(rf_b))
    print('cal_init   %5d u32' % len(cal))
    print('lmt_type0  %5d rows (%d 2T rows dropped)' % (len(lmt0), drop0))
    print('lmt_type5  %5d rows (%d 2T rows dropped)' % (len(lmt5), drop5))


if __name__ == '__main__':
    main()
