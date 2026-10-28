# Bluetooth (RTL8822CS/ES, UART / H5)

Status: porting plan / hardware-verified analysis, 2026-10-28.
Target: Linux 7.3, branch `mainline` (`/home/leftymods/atrios-mainline`).
Kernel sources used for the analysis:
`/home/leftymods/mainline-src/linux-7.3.tar.xz` (upstream 7.3; the archive is
truncated after `drivers/soc`, so `net/` and `drivers/tty/` had to be checked
against upstream v6.12/master instead), vendor tree
`AtriOS/AtriOS/AtriOS-linux`.

## TL;DR

| Item | Value |
|---|---|
| Transport | `&uart_A` = `serial@ffd24000` (0xffd24000), `ttyS1`, 4 wires TX/RX/CTS/RTS |
| Reset / enable | `GPIOX_17` (vendor 4.9 numbering: 83 = `0x53`), active high, driven as `enable-gpios` |
| Host wake | `GPIOX_19` (vendor: 85 = `0x55`), documented as `host-wake-gpios`, **not used** by `hci_h5` |
| Mainline stack | `hci_uart` + `hci_h5` (H5/3-wire) + `btrtl` over serdev |
| DT compatible | `realtek,rtl8822cs-bt` (the only RTL8822 entry upstream) |
| Firmware | `/lib/firmware/rtl_bt/rtl8822cs_fw.bin` + `rtl_bt/rtl8822cs_config.bin` |
| Vendor firmware | **not accepted as-is**: Realtek project id 33 is unknown to 7.3 `btrtl` |
| 8822ES | only a partial probe path in `btrtl` (rev `0x000e`), no firmware entry — must be measured |
| 4.9 branch | keep the Python H5 loader; the mainline kernel does not need it |

## 1. Vendor stack → mainline mapping

| Vendor (4.9.113) | Mainline 7.3 | Notes |
|---|---|---|
| platform driver `amlogic, bt-dev` (`bt-dev` node, `gpio_reset`, `gpio_hostwake`) | DT child node under `&uart_A` with `compatible = "realtek,rtl8822cs-bt"` | serdev device, no MFD/platform driver needed |
| `bt-dev` rfkill pulse in `/etc/init.d/S01modules` (1 → 0) | `h5_btrtl_open()`: enable 0 (100 ms) → 1 (500 ms) | `hci_h5.c:990-1022` |
| `usr/lib/atrios/rtk-bt-init` (Python H4/H5 loader) | `btrtl.c` (fw + config upload, baud rate, vendor commands) | not needed on mainline |
| Python H5 SLIP/CRC (`crc16_x25`, `bitrev16`, DIC) | `hci_h5.c` (SLIP, CRC-CCITT, DIC bit, link-control state machine, retransmit) | `hci_h5.c:44-49`, `144-147` |
| `etc/bluetooth/rtkbt.conf` (`BtDeviceNode=?/dev/ttyS1:H5`) | removed; the protocol is selected by the DT `compatible` | — |
| `/lib/firmware/rtlbt/rtl8822cs_*` | `/lib/firmware/rtl_bt/rtl8822cs_*.bin` | different directory **and** `.bin` suffix |
| `hciattach`/`rtk_hciattach` in userspace | kernel serdev probing at boot | no line discipline |

Vendor facts used above (all read from this repo):

- vendor DT node: `tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts:3810`
  `gpio_reset = <0x18 0x53 0x00>`, `gpio_hostwake = <0x18 0x55 0x00>`;
  `0x53 = 83 = GPIOX_17`, `0x55 = 85 = GPIOX_19`
  (`common/include/dt-bindings/gpio/meson-g12a-gpio.h:113,115`). Note: the
  upstream mainline header uses a different numbering base (`GPIOX_17` = 82,
  `GPIOX_19` = 84 in the current upstream file), so only the **symbols** are
  portable — the vendor `0x53`/`0x55` are 4.9 numbers.
- vendor UART pinctrl muxes all four lines:
  `sm1_s905x3_yandex.dts:1143` (`uart_tx_a`, `uart_rx_a`, `uart_cts_a`, `uart_rts_a`).
- vendor kernel has **no H5 support**: `common/arch/arm64/configs/meson64_defconfig:204-205`
  has `CONFIG_BT_HCIUART=y` and `CONFIG_BT_HCIUART_H4=y`, but no
  `BT_HCIUART_3WIRE`, no `BT_HCIUART_SERDEV`, no `BT_HCIUART_RTL`.
  That is why `hci0`/BlueZ cannot work and the Python loader exists.
- reset polarity: `enable-gpios` active high; the vendor rfkill state 0
  ("unblocked") is what leaves BT enabled (commit `2076b49`: "the old 0 -> 1
  left BT_EN low"). So the driver's 0 → 1 pulse releases the chip.

Caveat: `tools/fw_builder/README.md` still says "BT_EN: `GPIOX_18`"; the
vendor DTS (0x53 = 83) and `meson-g12a-gpio.h` say **GPIOX_17**. Use GPIOX_17;
confirm with a scope if the chip does not answer.

## 2. Kernel configuration

| Symbol | Needed | Why / reference |
|---|---|---|
| `CONFIG_BT` | yes | Bluetooth core (`net/bluetooth/Kconfig`) |
| `CONFIG_BT_BREDR` | yes | Classic profiles for BlueZ; default y |
| `CONFIG_BT_LE` | yes (for BLE) | 7.3 arm64 defconfig shows the symbol (`arch/arm64/configs/defconfig:184`) but leaves it off |
| `CONFIG_BT_HCIUART` | yes | `drivers/bluetooth/Kconfig:104` |
| `CONFIG_BT_HCIUART_SERDEV` | yes | `bool`, default y when `SERIAL_DEV_BUS` (`Kconfig:120`); builds `hci_serdev.o` (`Makefile:40`) |
| `CONFIG_BT_HCIUART_3WIRE` | yes | H5 protocol, builds `hci_h5.o` (`Kconfig:187`, `Makefile:45`); selects `CRC_CCITT` |
| `CONFIG_BT_HCIUART_RTL` | yes | Realtek glue (`Kconfig:227`); **selects** `BT_HCIUART_3WIRE` and `BT_RTL`; depends on `GPIOLIB` and on `ACPI || SERIAL_DEV_CTRL_TTYPORT` |
| `CONFIG_BT_RTL` | implied | hidden tristate, selected by `BT_HCIUART_RTL` (`Kconfig:14-16`) |
| `CONFIG_BT_HCIUART_H4` | **not needed** | H5 does its own framing; `hci_h5.c` uses no `h4_*` helper. Only enable if you want the legacy H4 line discipline |
| `CONFIG_SERIAL_DEV_BUS` | yes | serdev bus |
| `CONFIG_SERIAL_DEV_CTRL_TTYPORT` | yes | required by `BT_HCIUART_RTL` on non-ACPI (`Kconfig:232`); default y with serdev |
| `CONFIG_SERIAL_MESON` | yes | `uart_A` driver (`serial@24000` → `0xffd24000`) |
| `CONFIG_GPIOLIB` | yes | dependency of `BT_HCIUART_RTL` |
| `CONFIG_RFKILL` | recommended | rfkill nodes for hci0; `BT` depends on `RFKILL || !RFKILL` |
| `CONFIG_FW_LOADER` | yes | selected by `BT_RTL`; needed for fw/config |
| `CONFIG_CRC_CCITT` | implied | selected by `BT_HCIUART_3WIRE` |

`net/bluetooth/Kconfig` is missing from the local 7.3 tarball (truncated
archive); the `BT`/`BT_BREDR`/`BT_LE` symbols above were confirmed against the
current upstream file (v6.12), and `BT_LE` additionally against the 7.3 arm64
defconfig. Re-check them with `scripts/build-kernel.sh --strict` (it flags
symbols that do not exist/get dropped by `olddefconfig`).

The transport part is already in `mainline/configs/atrios.config:93-102`
(`SERIAL_DEV_BUS`, `SERIAL_DEV_CTRL_TTYPORT`, `BT`, `BT_HCIUART`,
`BT_HCIUART_SERDEV`, `BT_HCIUART_3WIRE`, `BT_HCIUART_RTL`). What is still
missing there is the BT core/profile part and rfkill; the complete set for a
usable BlueZ stack (bring-up: built in, not modules) is:

```
CONFIG_BT=y
CONFIG_BT_BREDR=y
CONFIG_BT_LE=y
CONFIG_BT_RFCOMM=y
CONFIG_BT_HIDP=y

CONFIG_BT_HCIUART=y
CONFIG_BT_HCIUART_SERDEV=y
CONFIG_BT_HCIUART_3WIRE=y
CONFIG_BT_HCIUART_RTL=y
CONFIG_BT_RTL=y

CONFIG_SERIAL_DEV_BUS=y
CONFIG_SERIAL_DEV_CTRL_TTYPORT=y
CONFIG_SERIAL_MESON=y
CONFIG_SERIAL_MESON_CONSOLE=y
CONFIG_GPIOLIB=y
CONFIG_RFKILL=y
CONFIG_FW_LOADER=y
CONFIG_CRC_CCITT=y

# not required for the serdev/H5 path:
# CONFIG_BT_HCIUART_H4 is not set
```

Why `=y` for bring-up: with `BT_HCIUART=m` the serdev child is only probed
after the module is loaded; `BT_RTL` is a separate module (`btrtl.o`,
`Makefile:25`). Built-in avoids module-autoload/ordering problems in the
Buildroot rootfs.

## 3. Device tree

Binding: `Documentation/devicetree/bindings/net/bluetooth/realtek,bluetooth.yaml`.
It allows **only** `compatible`, `device-wake-gpios`, `enable-gpios`,
`host-wake-gpios`, `max-speed` (`additionalProperties: false`, lines 19-52),
plus the generic serial-peripheral properties. There is **no** `vcc-supply`,
no `shutdown-gpios` and no `firmware-name` property. The reset line must be
described as `enable-gpios`.

Driver consumption (`hci_h5.c`):

- `enable-gpios` — requested at probe (`hci_h5.c:894`), pulsed 0 → 1 in
  `h5_btrtl_open()` (100 ms / 500 ms, `hci_h5.c:1013-1021`).
- `device-wake-gpios` — requested at probe (`hci_h5.c:898`); optional and
  **absent on this board** (GPIO helpers tolerate a NULL descriptor).
- `host-wake-gpios` — **never requested** by `hci_h5`; it only exists in the
  ACPI GPIO map (`hci_h5.c:1096-1103`). Putting it in DT is documentation for
  a future wake-up implementation, it has no runtime effect today.
- `max-speed` — not read by `hci_h5.c`/`hci_serdev.c`; the operating baud rate
  comes from the Realtek config file (see §4).

Reference fragment (text only — do not put it in `mainline/dts` from this
doc). The tree already contains the same node, without the optional
`host-wake-gpios`, in `mainline/dts/meson-sm1-yandex-station-max.dts`:

```dts
&uart_A {
	status = "okay";
	pinctrl-0 = <&uart_a_pins>, <&uart_a_cts_rts_pins>;
	pinctrl-names = "default";
	uart-has-rtscts;

	bluetooth {
		compatible = "realtek,rtl8822cs-bt";

		/* reset / BT_EN; GPIOX_17 is 0x53 in the vendor DT */
		enable-gpios = <&gpio GPIOX_17 GPIO_ACTIVE_HIGH>;

		/* BT -> host wake, GPIOX_19 = 85 = 0x55.
		 * Documented for completeness; hci_h5 does not use it.
		 */
		host-wake-gpios = <&gpio GPIOX_19 GPIO_ACTIVE_HIGH>;
	};
};
```

Notes:

- `uart_a_pins` / `uart_a_cts_rts_pins` exist in 7.3:
  `arch/arm64/boot/dts/amlogic/meson-g12-common.dtsi:1501,1510`.
- `uart_A` itself is `serial@24000` (→ `0xffd24000`) in the same file at
  line 2412; `compatible = "amlogic,meson-g12a-uart", "amlogic,meson-gx-uart"`.
- `uart-has-rtscts` is required in practice: the Realtek config enables
  hardware flow control (§4), and `h5_btrtl_setup()` calls
  `serdev_device_set_flow_control(..., true)`.
- Do not add `reset-gpios`: it would fail `dtbs_check` because of
  `additionalProperties: false`.
- With serdev bound, `/dev/ttyS1` is **not** created — the port belongs to
  the kernel driver. Anything in userspace that opens `/dev/ttyS1` must go.

## 4. Firmware

### What `btrtl` loads

For the 8822CS entries (`btrtl.c:243-258`) the driver always appends `.bin`:

- `rtl_bt/rtl8822cs_fw.bin`
- `rtl_bt/rtl8822cs_config.bin` (mandatory: `config_needed = true`)

`btrtl.c:1218-1236` — the config file is loaded **only when the chip's
`RTL_SEC_PROJ` key id is 0**; if it is non-zero the config is skipped and
`h5_btrtl_setup()` later fails at `btrtl_get_uart_settings()` with
`"RTL: no config loaded"` (`btrtl.c:1453-1457`). This is an open risk on this
board until dmesg shows `RTL: ... key id 0`.

### Vendor files in this repo

Path: `board/yandex/station2/rootfs_overlay/lib/firmware/rtlbt/`

| File | Size | sha256 |
|---|---|---|
| `rtl8822cs_fw`, `rtl8822cs_fw.bin`, `rtl8822es_fw`, `rtl8822es_fw.bin` | 44064 | `ce5d6aff…c23dc727` |
| `rtl8822cs_config`, `.bin`, `rtl8822es_config`, `.bin` | 85 | `98572d9b…c8393ff` |

All four "fw" files are byte-identical, and all four "config" files are
byte-identical (verified with `cmp`/`sha256sum`). The `8822es`/`8822cs` split
is only a naming convention of the vendor drop.

Format (parsed with the 7.3 structures from `btrtl.h`/`btrtl.c`):

- firmware: `RTBTCore` (V2), 1 section `opcode 0x1` (snippets), 1 subsection
  `eco=1, prio=0x0f, len=44013`, 11-byte tail
  `ff 02 01 01 21 01 00 51 04 fd 77` (extension signature `51 04 fd 77`).
  This is a format 7.3 understands (`RTL_EPATCH_SIGNATURE_V2`, `btrtl.c:683`).
- The project-id instruction in the tail decodes to **project id 33 (0x21)**.
- config: magic `0x8723ab55`, `total_len = 0x4f (79)`, one entry
  `offset 0x0c, len 0x10`, device baudrate `0x04928001` → **3,000,000**,
  flow control bit (`data[12] & BIT(2)`) **set**.

### Verdict: vendor firmware is path-compatible but content-incompatible

`rtlbt_parse_firmware()` checks the project id against
`project_id_to_lmp_subver[]` (`btrtl.c:657-675`: ids 0,1,2,3,7,8,9,10,13,14,
18,20,25,36,44,47,51 — **no 33**). Therefore loading the vendor file ends in

```
RTL: unknown project id 33
```

and the firmware download is aborted (`btrtl.c:740-743`). Renaming/copying the
files to `rtl_bt/` does not help. The **config** file, on the other hand, is
valid for 7.3 and would work as-is (3 Mbaud + flow control).

Check the project id of any Realtek BT firmware file (same walk as
`rtlbt_parse_firmware()`):

```sh
python3 - /lib/firmware/rtl_bt/rtl8822cs_fw.bin <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
assert d[:8] in (b'RTBTCore', b'Realtech'), d[:8]
p = len(d) - 4                      # skip extension signature
while p >= 17:
    p -= 3
    op, ln, data = d[p], d[p + 1], d[p + 2]
    if op == 0xff: break
    if ln == 0: break
    if op == 0 and ln == 1:
        print('project id', data); break
    p -= ln
PY
```

Two ways forward:

1. **Preferred first attempt:** take the official linux-firmware
   `rtl_bt/rtl8822cs_fw.bin` + `rtl_bt/rtl8822cs_config.bin` (btrtl's own
   table expects project id 13 for 8822C; verify the downloaded file with the
   snippet above). Keep them unmodified.
2. **If only the vendor patch runs on this silicon:** one-line table addition
   in `btrtl.c`:

   ```c
   { RTL_ROM_LMP_8822B, 33 },   /* vendor 8822CS/ES firmware */
   ```

   plus `MODULE_FIRMWARE("rtl_bt/rtl8822cs_fw.bin")` already exists. With
   that, the V2 parser accepts the vendor file; the subsection is applied only
   if `rom_version == 0` (the parser compares `eco` with `rom_version + 1`,
   `btrtl.c:516-517`), otherwise it returns `-ENODATA`.

For a 115200 first bring-up (avoids 3 Mbaud on a fresh port) the vendor
loader's trick can be reused: write `14 c0 52 02` at config offset 9..12
(`entry->data[0..3]`), which `btrtl_convert_baudrate()` maps to 115200
(`btrtl.c:1406-1440`); the flow-control bit in `data[12]` is unaffected.
Do this on a copy of the file, keep the original.

## 5. RTL8822ES: does 7.3 support it?

**No complete support — only a probe special case.** Evidence:

- The 8822ES is not in the `compatible` list: only
  `realtek,rtl8822cs-bt` exists (`hci_h5.c:1142-1155`,
  `realtek,bluetooth.yaml:20-30`). The DT node must use the CS compatible.
- `btrtl.c` has one 8822E-specific block (`btrtl.c:1094-1113`): after reading
  `RTL_CHIP_SUBVER` (`= 0x8822`) it reads `RTL_CHIP_REV`; if the value is
  `0x000e` it forces `hci_ver = lmp_ver = 0x0c` and calls
  `btrtl_match_ic(0x8822, 0x000e, 0x0c, HCI_UART, 0)`.
- The 7.3 `ic_id_table` has **no** `hci_rev == 0x0e` entry (8822 rows are
  rev `0x0b`/`0x0c` with hci_ver `0x07`/`0x08`/`0x0a`), so the match is NULL.
  The generic "unknown IC" fallback then sends vendor command `0xfc66`
  ("drop firmware") once and re-runs detection (`btrtl.c:1140-1169`).
- If the second detection still reports `0x000e`, `btrtl` logs
  `RTL: unknown IC info, lmp subver 8822, hci rev 000e, hci ver 000c`
  (`btrtl.c:1171-1175`) and returns without firmware/config; `h5_btrtl_setup()` then dies in
  `btrtl_get_uart_settings()` and `hci0` stays down.
- If the second detection reports a table value (e.g. rev `0x000c`), the chip
  is driven as **RTL8822CS** and uses `rtl_bt/rtl8822cs_fw.bin`.
- There is no `rtl8822es` firmware name anywhere in 7.3 `btrtl`
  (`MODULE_FIRMWARE` list, `btrtl.c:1549-1550`), no project id 33, and no
  `0xa822` reference (that is the SDIO Wi-Fi id, handled by rtw88 — also
  missing, see the Wi-Fi section of `porting-plan.md`).

What to measure on hardware (this decides everything):

1. Boot the DT node with the config from §2 and read dmesg
   (`RTL: examining hci_ver=.. hci_rev=.. lmp_ver=.. lmp_subver=8822`).
2. Case A — `hci_rev=000c`: the chip matches the 8822CS UART entry; just use
   the official `rtl8822cs` firmware.
3. Case B — `hci_rev=000e` and then `unknown IC info`: 8822E is not usable
   with the stock table. Plan:

   - add the `8822E` `ic_id_table` entry (rev `0x0e`, hci_ver `0x0c`,
     `HCI_UART`, `fw_name = "rtl_bt/rtl8822es_fw"`,
     `cfg_name = "rtl_bt/rtl8822es_config"`, `config_needed`,
     `has_rom_version`; `has_msft_ext` to be confirmed);
   - add `{ RTL_ROM_LMP_8822B, 33 }` to `project_id_to_lmp_subver[]`;
   - add both `MODULE_FIRMWARE` strings;
   - install the vendor files as `rtl_bt/rtl8822es_{fw,config}.bin`
     (byte-identical to the CS pair, so either name works) and re-test the
     `eco`/`rom_version` match, then baud rate and flow control;
   - if `rom_version != 0` refuses the subsection, try the official CS
     firmware instead or relax the `eco` filter for this chip.
4. Do **not** plan on the Python H5 loader as a mainline fallback: it drives
   the UART raw and only implements a scanner, it does not register an HCI
   device for BlueZ. Without `hci0` there is no Bluetooth stack. The loader is
   a 4.9-only workaround.

## 6. Bring-up and verification

Files on the target (Buildroot rootfs, reused from `main`):

```
/lib/firmware/rtl_bt/rtl8822cs_fw.bin
/lib/firmware/rtl_bt/rtl8822cs_config.bin
```

Kernel messages to expect (successful 8822CS path):

```
Bluetooth: HCI UART driver ver ...
Bluetooth: HCI UART protocol Three-wire (H5) registered
Bluetooth: hci0: RTL: examining hci_ver=0a hci_rev=000c lmp_ver=0c lmp_subver=8822
Bluetooth: hci0: RTL: rom_version status=0 version=0
Bluetooth: hci0: RTL: btrtl_initialize: key id 0
Bluetooth: hci0: RTL: loading rtl_bt/rtl8822cs_fw.bin
Bluetooth: hci0: RTL: loading rtl_bt/rtl8822cs_config.bin
Bluetooth: hci0: RTL: cfg_sz 85, total sz ...
Bluetooth: hci0: RTL: fw version 0x...
```

Commands:

```sh
dmesg | grep -iE 'Bluetooth|hci|RTL'
ls /sys/class/bluetooth/                 # hci0 must appear
rfkill list                              # "hci0: Bluetooth", not blocked
hciconfig -a                             # UP/RUNNING, BD address
hcitool dev
hciconfig hci0 up
hcitool scan                             # classic inquiry
```

Notes:

- The current Buildroot config builds `bluez5_utils` **without**
  `BR2_PACKAGE_BLUEZ5_UTILS_CLIENT` (`buildroot/.config:3036`), so
  `bluetoothctl` is not installed; `hciconfig`, `hcitool`, `hcidump`,
  `hciattach` are (`BR2_PACKAGE_BLUEZ5_UTILS_TOOLS` + `_DEPRECATED`). Either
  enable the client option or use `hcitool`/`btmgmt`.
- `/dev/ttyS1` disappears once serdev binds; that is expected. Check
  `/sys/bus/serial/devices/` for the `serial0-0` style device instead.
- Failure signatures and meaning:

| dmesg | Meaning |
|---|---|
| `RTL: unknown IC info ... hci rev 000e` | 8822E, no table entry → §5 case B |
| `RTL: unknown project id 33` | vendor firmware, not the official one → §4 |
| `RTL: mandatory config file rtl_bt/rtl8822cs_config not found` | config missing/wrong name |
| `RTL: no config loaded` | `key id != 0` (config skipped) or empty config |
| `RTL: no UART config entry found` | config without offset `0x0c` entry |
| hci0 never reaches UP, setup error in dmesg | baud rate/flow control problem, see §7 |

## 7. Risks / open items

1. **Chip identity (blocker).** 8822E vs 8822CS is decided by the vendor
   register `0x3a` read at `btrtl.c:1094-1113`. Unknown until measured.
2. **Firmware acceptance.** Vendor files declare project id 33; 7.3 rejects
   them. Use linux-firmware or patch `btrtl` (both plans in §4/§5).
3. **key id.** If `RTL_SEC_PROJ` is not 0, 7.3 skips the config and H5 setup
   fails with `no config loaded`. No upstream workaround; would need a local
   patch (e.g. fall back to 115200 / no config).
4. **3 Mbaud.** The vendor config requests 3 Mbaud + HW flow control. The
   Meson UART can generate it (24 MHz xtal / 8), but the four-wire link is
   unproven at that rate. For bring-up use the 115200 config variant (§4).
5. **Flow control / CTS-RTS.** `uart-has-rtscts` and both pinctrl groups are
   mandatory; without CTS/RTS the controller can overrun at 3 Mbaud.
6. **Reset polarity.** `enable-gpios` is ACTIVE_HIGH based on the vendor
   rfkill semantics. If BT stays silent, try ACTIVE_LOW before suspecting the
   firmware; the pulse shape (100 ms low, 500 ms high) is the driver's and the
   vendor used 150 ms / 400 ms — same semantics.
7. **Host wake is not implemented.** GPIOX_19 is unused by `hci_h5`; there is
   no wake-on-Bluetooth. If the product needs it, it is a separate patch
   (request `host-wake` GPIO + IRQ + `hdev->wakeup`).
8. **Power sequencing.** The vendor init powers the Wi-Fi side
   (`wifi-power`) *before* pulsing BT rfkill. Mainline controls only
   `GPIOX_17`. If the combo needs WL_REG_ON/shared rail up for BT, BT will
   not answer; check the module schematic/rail before debugging firmware.
9. **Suspend/resume.** For 8822CS `H5_INFO_WAKEUP_DISABLE` is **not** set
   (`hci_h5.c:1118-1123`), so the driver uses runtime PM and
   `h5_btrtl_suspend/resume`; without a `device-wake` line resume only
   re-enables flow control. Needs a suspend test once basic bring-up works.
10. **Userspace leftovers.** `S01modules`, `rtk-bt-init` and
    `usr/lib/atrios/wireless bt` are 4.9-specific (they `insmod` 4.9 modules
    and poke `/dev/ttyS1`). They must not run on the mainline image, and
    `rfkill` pulsing is no longer needed (the driver does it).
11. **Stale docs.** `tools/fw_builder/README.md` names `GPIOX_18` for BT_EN;
    vendor DT/header say GPIOX_17.

## 8. 4.9 branch

Nothing here changes the 4.9 image: the Python `rtk-bt-init` H5/H4 loader
remains the only way to talk to the chip there, because the vendor kernel has
no `BT_HCIUART_3WIRE`/serdev support (`meson64_defconfig:204-205`). On the
mainline kernel the loader is unnecessary and must be disabled — `hci_uart`
probes the DT child node by itself, uploads firmware through `btrtl`, and
exposes a normal `hci0` to BlueZ.

## 9. References

Upstream 7.3 (line numbers from the extracted tree):

- `drivers/bluetooth/hci_h5.c` — H5 protocol, serdev probe, RTL vendor hooks
  (`:44-49` header macros, `:144-147` DIC, `:855-905` probe, `:937-988`
  btrtl setup, `:990-1022` enable pulse, `:1096-1112` ACPI GPIO map,
  `:1142-1155` OF match).
- `drivers/bluetooth/hci_serdev.c` — serdev glue.
- `drivers/bluetooth/btrtl.c` — IC table `:108-336`, project ids `:657-675`,
  project check `:740-750`, 8822E probe `:1094-1113`, drop-fw retry
  `:1140-1169`, config load `:1218-1236`, baud rate map `:1406-1440`,
  UART settings `:1443-1500`.
- `drivers/bluetooth/Kconfig` — `BT_HCIUART` `:104`, `_SERDEV` `:120`,
  `_3WIRE` `:187`, `_RTL` `:227`.
- `Documentation/devicetree/bindings/net/bluetooth/realtek,bluetooth.yaml`.
- `arch/arm64/boot/dts/amlogic/meson-g12-common.dtsi` — `uart_A` `:2412`,
  pin groups `:1501,1510`.

Repo:

- vendor DT `tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts:2687`
  (`serial@ffd24000`), `:3810` (`bt-dev`, GPIO numbers), `:1143` (UART pins).
- vendor config `common/arch/arm64/configs/meson64_defconfig:204-205`.
- mainline kernel fragment `mainline/configs/atrios.config:93-102`.
- vendor loader `board/yandex/station2/rootfs_overlay/usr/lib/atrios/rtk-bt-init`
  (H4/H5, SLIP, CRC-16/X25, DIC, 115200 config patch at offset 9).
- vendor init `board/yandex/station2/rootfs_overlay/etc/init.d/S01modules`
  (rfkill pulse; 4.9 modules).
- firmware `board/yandex/station2/rootfs_overlay/lib/firmware/rtlbt/`.
- existing mainline DT node `mainline/dts/meson-sm1-yandex-station-max.dts`
  (`&uart_A` → `bluetooth`; line numbers drift, grep for `realtek,rtl8822cs-bt`).
