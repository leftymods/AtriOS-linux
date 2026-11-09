import socket
import struct
import sys
import os
import time

NETLINK_GENERIC = 16
SOL_NETLINK = 270
NETLINK_ADD_MEMBERSHIP = 1

NLM_F_REQUEST = 1
NLM_F_ACK = 4
NLM_F_ROOT = 0x100
NLM_F_MATCH = 0x200
NLM_F_DUMP = NLM_F_ROOT | NLM_F_MATCH

GENL_ID_CTRL = 0x10
CTRL_CMD_GETFAMILY = 3
CTRL_ATTR_FAMILY_ID = 1
CTRL_ATTR_FAMILY_NAME = 2
CTRL_ATTR_MCAST_GROUPS = 7
CTRL_ATTR_MCAST_GRP_NAME = 1
CTRL_ATTR_MCAST_GRP_ID = 2

NL80211_CMD_GET_SCAN = 32
NL80211_CMD_TRIGGER_SCAN = 33
NL80211_CMD_NEW_SCAN_RESULTS = 34

NL80211_ATTR_IFINDEX = 3
NL80211_ATTR_SCAN_SSIDS = 45
NL80211_ATTR_BSS = 47

NL80211_BSS_BSSID = 1
NL80211_BSS_FREQUENCY = 2
NL80211_BSS_SIGNAL_MBM = 7
NL80211_BSS_INFORMATION_ELEMENTS = 6

def nla(attr_type, data):
    length = len(data) + 4
    pad = (4 - (length % 4)) % 4
    return struct.pack('HH', length, attr_type) + data + (b'\x00' * pad)

def parse_nla(payload):
    attrs = {}
    offset = 0
    while offset + 4 <= len(payload):
        nla_len, nla_type = struct.unpack('HH', payload[offset:offset+4])
        if nla_len < 4:
            break
        data = payload[offset+4:offset+nla_len]
        attrs[nla_type & 0x3fff] = data
        pad = (4 - (nla_len % 4)) % 4
        offset += nla_len + pad
    return attrs

def get_family_info(s):
    req = struct.pack('BBH', CTRL_CMD_GETFAMILY, 1, 0)
    req += nla(CTRL_ATTR_FAMILY_NAME, b'nl80211\x00')
    nlh = struct.pack('IHHII', len(req) + 16, GENL_ID_CTRL, NLM_F_REQUEST, 1, 0)
    s.send(nlh + req)
    resp = s.recv(4096)
    attrs = parse_nla(resp[20:])
    fam_id = struct.unpack('H', attrs[CTRL_ATTR_FAMILY_ID])[0]
    
    scan_mcast = None
    if CTRL_ATTR_MCAST_GROUPS in attrs:
        grps = parse_nla(attrs[CTRL_ATTR_MCAST_GROUPS])
        for grp_data in grps.values():
            grp_attrs = parse_nla(grp_data)
            name = grp_attrs.get(CTRL_ATTR_MCAST_GRP_NAME, b'').rstrip(b'\x00').decode('ascii', errors='ignore')
            grp_id = struct.unpack('I', grp_attrs.get(CTRL_ATTR_MCAST_GRP_ID, b'\x00\x00\x00\x00'))[0]
            if name == 'scan':
                scan_mcast = grp_id
    return fam_id, scan_mcast

def main():
    ifindex = socket.if_nametoindex("wlan0")
    s = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, NETLINK_GENERIC)
    s.bind((0, 0))
    
    fam_id, scan_mcast = get_family_info(s)
    print(f"nl80211 ID: {fam_id}, scan mcast group: {scan_mcast}")
    if scan_mcast:
        s.setsockopt(SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, scan_mcast)
    
    s.settimeout(12.0)
    
    print("Triggering ACTIVE scan on wlan0 with wildcard SSID...")
    req = struct.pack('BBH', NL80211_CMD_TRIGGER_SCAN, 1, 0)
    req += nla(NL80211_ATTR_IFINDEX, struct.pack('I', ifindex))
    # Active scan: pass empty SSID inside SCAN_SSIDS
    req += nla(NL80211_ATTR_SCAN_SSIDS, nla(1, b''))
    
    nlh = struct.pack('IHHII', len(req) + 16, fam_id, NLM_F_REQUEST | NLM_F_ACK, 77, 0)
    s.send(nlh + req)
    
    # Wait for scan result event or timeout
    start = time.time()
    scan_done = False
    while time.time() - start < 10.0:
        try:
            resp = s.recv(4096)
            if len(resp) >= 20:
                nlh_len, nlh_type, flags, seq, pid = struct.unpack('IHHII', resp[:16])
                if nlh_type == fam_id:
                    cmd, ver, res = struct.unpack('BBH', resp[16:20])
                    if cmd == NL80211_CMD_NEW_SCAN_RESULTS:
                        print(">>> Received NL80211_CMD_NEW_SCAN_RESULTS event!")
                        scan_done = True
                        break
        except socket.timeout:
            break
        except Exception as e:
            print(f"recv note: {e}")
            break
            
    print("Fetching scan results (NL80211_CMD_GET_SCAN)...")
    req = struct.pack('BBH', NL80211_CMD_GET_SCAN, 1, 0)
    req += nla(NL80211_ATTR_IFINDEX, struct.pack('I', ifindex))
    nlh = struct.pack('IHHII', len(req) + 16, fam_id, NLM_F_REQUEST | NLM_F_DUMP, 78, 0)
    s.send(nlh + req)
    
    results = []
    while True:
        try:
            resp = s.recv(16384)
        except Exception:
            break
        offset = 0
        done = False
        while offset + 16 <= len(resp):
            nlh_len, nlh_type, flags, seq, pid = struct.unpack('IHHII', resp[offset:offset+16])
            if nlh_len < 16 or offset + nlh_len > len(resp):
                break
            if nlh_type == 3: # NLMSG_DONE
                done = True
                break
            payload = resp[offset+16:offset+nlh_len]
            if len(payload) >= 4:
                attrs = parse_nla(payload[4:])
                if NL80211_ATTR_BSS in attrs:
                    bss_attrs = parse_nla(attrs[NL80211_ATTR_BSS])
                    bssid = bss_attrs.get(NL80211_BSS_BSSID)
                    freq = bss_attrs.get(NL80211_BSS_FREQUENCY)
                    sig = bss_attrs.get(NL80211_BSS_SIGNAL_MBM)
                    ies = bss_attrs.get(NL80211_BSS_INFORMATION_ELEMENTS)
                    
                    ssid = "<Hidden>"
                    if ies:
                        ie_off = 0
                        while ie_off + 2 <= len(ies):
                            ie_id, ie_len = struct.unpack('BB', ies[ie_off:ie_off+2])
                            if ie_id == 0 and ie_off + 2 + ie_len <= len(ies):
                                try:
                                    ssid = ies[ie_off+2:ie_off+2+ie_len].decode('utf-8', errors='ignore')
                                except Exception:
                                    pass
                                break
                            ie_off += 2 + ie_len
                    bssid_str = ":".join(f"{b:02x}" for b in bssid) if bssid else "unknown"
                    freq_mhz = struct.unpack('I', freq)[0] if freq else 0
                    signal_dbm = (struct.unpack('i', sig)[0] / 100.0) if sig else 0.0
                    results.append((ssid, bssid_str, freq_mhz, signal_dbm))
            offset += (nlh_len + 3) & ~3
        if done:
            break
            
    print(f"\nDiscovered {len(results)} Wi-Fi Networks:")
    print(f"{'SSID':<32} {'BSSID':<20} {'Freq':<10} {'Signal':<10}")
    print("=" * 75)
    for ssid, bssid, freq, sig in sorted(results, key=lambda x: x[3], reverse=True):
        print(f"{ssid:<32} {bssid:<20} {freq:<10} {sig:<10.1f} dBm")

if __name__ == "__main__":
    main()
