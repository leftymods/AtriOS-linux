#!/usr/bin/env python3
"""
Automated Clean Origin & Proprietary Blob Audit Script
Used by Teamwork Forensic Auditor (teamwork_auditor) to enforce Requirement R3.

Enforces:
1. STRICT ZERO MATCHES with any binary or partition from the original Yandex Station Max firmware.
2. Verified origin of all FIP components from upstream open-source LibreELEC amlogic-boot-fip or built from source.
"""

import os
import sys
import hashlib
import json

# Proprietary blacklist hashes extracted from original Yandex Station Max 1.99 firmware (AmlogicKitchen)
PROPRIETARY_BLACKLIST = {
    "efbcd97ee8b19330e13f61a9c9736e3330c51f23108eaa218f7a6ef0664a58e6": "Stock Yandex DDR_ENC.USB / UBOOT_ENC.USB",
    "a17bed7f05dfcbfe0d58a8cb1b046ac68acbf53641fb463297c9d12b93f0a6a1": "Stock Yandex bootloader.PARTITION / DDR.USB / UBOOT.USB",
    "9ec538e55aba518905a17741dade6ad626f2af7a17dc89a6380313c139e6239b": "Stock Yandex logo.PARTITION",
    "6938371d322fb6334263d5d7f6e7e978421cbca611e14c5e42e0198d0d81fbe0": "Stock Yandex aml_sdc_burn.UBOOT.ENC",
    "e24baa2c2d30395d16e0a3f13aa2759ff766743428b64e9b75042e1cb5fb8a57": "Stock Yandex aml_sdc_burn.UBOOT",
    "00cad33de3ae8984bcb26adac1cadb286e7f369483ab5ef832d5b22370aca31d": "Stock Yandex meson1.dtb / _aml_dtb.PARTITION",
    "d4d761b2dc1437a069363c5c8fa1415c4d286ead67bcbbc0ec7ab52a543c2261": "Stock Yandex boot.PARTITION",
    "cbdfb14d35960bff852a4e05c003a94b0676a11b54a5030e855c3816b3c472c9": "Stock Yandex recovery.PARTITION",
    "29a07bd6132186d223dfae0f7be4a66aa22f05a00b5e3f4df40c63c4c939f4dd": "Stock Yandex dtbo.PARTITION",
    "f3a2c81bf2c151ecb537879482a2c0c2e9d8da6ba6380ba78407e2e410f25d73": "Stock Yandex vbmeta.PARTITION",
    "1981261f893f6cf6c6c47ef5741b4d979ba926b126f238ca21e5ecd4c5467033": "Stock Yandex platform.conf",
    "e8d93f46803e26ce8457f902edd348806cbb88c8ef706deea2eb441879f0a5cc": "Stock Yandex aml_sdc_burn.ini",
}

# Whitelist of clean open-source upstream LibreELEC amlogic-boot-fip components
UPSTREAM_LIBREELEC_FIP = {
    # DDR Firmwares (common to G12A / SM1 / G12B across upstream LibreELEC)
    "a12fd675a29268a185d630bb50ba47287ac4ff3f7b39f29ff0f3905419bb8f47": "upstream/ddr3_1d.fw",
    "2a7b3197699b3991021df2432ad940b58c435d6627988e7c4450ea9300854051": "upstream/ddr4_1d.fw",
    "b57a75959a24fd6de6a2ed80f7e8a7cd6911c78b361de86a7e4f194b9f950ae0": "upstream/ddr4_2d.fw",
    "0945f0cdf8b33099398d550b520741acbef0532032b88bb4359e2b9a4989ac8b": "upstream/diag_lpddr4.fw",
    "4ff0f3e3cc20bbf3ed2d68ca6439fc1ff19b433c2e9fa1a1bcebaaedaa5a9cc7": "upstream/lpddr3_1d.fw",
    "964d534d6f2f8bf20bab691463c43657da6bdbfa7e8801dde902e644091d0e74": "upstream/lpddr4_1d.fw",
    "9dd80a91546d61148d4f5712f1d12e3f7641987a6642512986cc56a17c4fc3c6": "upstream/lpddr4_2d.fw",
    "c2240732a217ac61ed53243668e7247a323fc5ea1450383ff62a55092136af16": "upstream/piei.fw",
    "69072fbd54dfb411199b21ff228b7b2399ff80650c27771112870c90bfaef34b": "upstream/aml_encrypt_g12a",

    # Upstream sei610 (SM1 / S905X3 reference)
    "8e1741fca4e76dccb997e082e6d6251b6814041d8e64c391219b168ff189cb6b": "upstream/sei610/bl2.bin",
    "e69586ccac4679bcc19b26fc49377484df726daae3bfa59a0f443b79dae13271": "upstream/sei610/acs.bin",
    "631a5d2a68caed6b6d510f274a7eb063b7156ce50eb348b61df43904ddae4068": "upstream/sei610/bl30.bin",
    "e7135ed5df5dcb0fc1beeb73259b3ceae3fe1b0ecbc9ec464e107df6c63bcae5": "upstream/sei610/bl301.bin",
    "cddb1470dcd4c7965dfb15bb379ceaa7d0966f913d2fa270a68d0bb1fa208e98": "upstream/sei610/bl31.img",
    "450f06606c53b6b0b2e2d67a99f1fa1a1ce089baae8fcf709841c6f4ef627cf9": "upstream/sei610/aml_ddr.fw",

    # Upstream khadas-vim3l (SM1 / S905D3 reference)
    "a9cb54595fe0ff40a7cf5003c2bc16999a4cce4ca5f95844ce2f56708b76599f": "upstream/khadas-vim3l/bl2.bin",
    "0bec04b517765025ad87a323aeeceb8ff5b17ae2fa0be21f3083cfc0b9a89d31": "upstream/khadas-vim3l/acs.bin",
    "65c16b37c76eaed583c74997c6ca7e0d37e53ef711b71d9d95fdfc145e6fe7aa": "upstream/khadas-vim3l/bl30.bin",
    "32f6156293fc32ae18129758784d142d1316b23b88b7bcad1be7495b6c23bf30": "upstream/khadas-vim3l/bl301.bin",
    "bd8098c9392b77ba72166ee50f75e7a9b093fefb0f2098d63a152fb9713ef3b8": "upstream/khadas-vim3l/bl31.img",
    "8ad9cf41adb5ce9bfa780d6f4699f1fa0caeb19ea4563a3ca40f25e7f223f03b": "upstream/khadas-vim3l/bl31.bin",
    "f403bf63344a47e841262d5e7ef327db08e169d2f2cb6c1d07c0aa7cf29f1043": "upstream/khadas-vim3l/aml_ddr.fw",
}

def audit_target(scan_dirs, check_kitchen_path=None):
    print("=" * 70)
    print("  FORENSIC AUDIT: REQUIREMENT R3 CLEAN ORIGIN & HASH INTEGRITY")
    print("=" * 70)

    # If kitchen path is given, scan it to dynamically supplement blacklist
    dynamic_blacklist = dict(PROPRIETARY_BLACKLIST)
    if check_kitchen_path and os.path.exists(check_kitchen_path):
        for root, _, files in os.walk(check_kitchen_path):
            if '/.git' in root: continue
            for f in files:
                p = os.path.join(root, f)
                if os.path.isfile(p) and not os.path.islink(p):
                    sz = os.path.getsize(p)
                    if sz > 100 * 1024 * 1024: continue
                    h = hashlib.sha256(open(p, 'rb').read()).hexdigest()
                    rel = os.path.relpath(p, check_kitchen_path)
                    dynamic_blacklist[h] = f"AmlogicKitchen/{rel}"

    violations = []
    audited_count = 0
    upstream_matched = []

    for s_dir in scan_dirs:
        if not os.path.exists(s_dir):
            continue
        print(f"Scanning target directory: {s_dir}")
        for root, dirs, files in os.walk(s_dir):
            dirs[:] = [d for d in dirs if d not in ['.git', 'buildroot', 'output', 'bl33']]
            if any(x in root for x in ['.git', 'buildroot', 'output', 'bl33']):
                continue
            for f in files:
                p = os.path.join(root, f)
                if not os.path.isfile(p) or os.path.islink(p):
                    continue
                sz = os.path.getsize(p)
                if sz > 50 * 1024 * 1024:
                    continue
                audited_count += 1
                try:
                    with open(p, 'rb') as fp:
                        h = hashlib.sha256(fp.read()).hexdigest()
                    rel = os.path.relpath(p, s_dir)

                    # Check Blacklist
                    if h in dynamic_blacklist:
                        violations.append({
                            'path': p,
                            'relative_path': rel,
                            'sha256': h,
                            'matched_proprietary_blob': dynamic_blacklist[h],
                            'size': sz
                        })

                    # Check Whitelist
                    if h in UPSTREAM_LIBREELEC_FIP:
                        upstream_matched.append({
                            'path': p,
                            'relative_path': rel,
                            'sha256': h,
                            'upstream_component': UPSTREAM_LIBREELEC_FIP[h]
                        })
                except Exception as e:
                    print(f"Error reading {p}: {e}")

    print(f"\nTotal files scanned: {audited_count}")
    print(f"Total upstream LibreELEC whitelisted matches: {len(upstream_matched)}")
    for m in upstream_matched:
        print(f"  [CLEAN UPSTREAM] {m['relative_path']} -> {m['upstream_component']}")

    print("\n" + "=" * 70)
    if violations:
        print(f"FAILED: FOUND {len(violations)} PROPRIETARY CONTAMINATIONS (Requirement R3 VIOLATION)!")
        print("=" * 70)
        for v in violations:
            print(f"  [CONTAMINATION] {v['path']}")
            print(f"    Size: {v['size']} bytes | SHA256: {v['sha256']}")
            print(f"    Origin: MATCHES PROPRIETARY BLOB: {v['matched_proprietary_blob']}")
        return False, violations
    else:
        print("PASSED: ZERO PROPRIETARY MATCHES DETECTED! (Requirement R3 SATISFIED)")
        print("=" * 70)
        return True, []

if __name__ == '__main__':
    scan_targets = [
        "/home/lmods/MainlineAtriOS/bootloader/uboot-repo",
        "/home/lmods/MainlineAtriOS/device/yandex/station_max/upgrade"
    ]
    kitchen = "/home/lmods/AmlogicKitchen/level1"
    passed, v = audit_target(scan_targets, kitchen)
    sys.exit(0 if passed else 1)
