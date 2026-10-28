# Bootloader assets

These files are the exact inputs for the `bootloader`, `bootloader2` and
`U-Boot (SD)` partitions. They are plain repository assets, so a fresh clone
builds the same bootloader as the released images. Do not replace them with
assets from another U-Boot build.

| File | Contents | Size | SHA256 |
|---|---|---|---|
| `bootloader.img` | U-Boot 2015.01 (LZ4 FIP, the tested v112 build) | 1 453 936 | `69766ddefd7949dc82252422b8e8adc5fbb904947bf16191ef8e4048a40345e6` |
| `u-boot.bin.sd.bin` | same U-Boot packaged for SD burning | 1 454 448 | `b63e9d693dce13d48362f4af1fd5dbf4b37e11a8345c77b9a244550db74a7850` |
| `platform.conf` | Amlogic burn configuration | 155 | `1981261f893f6cf6c6c47ef5741b4d979ba926b126f238ca21e5ecd4c5467033` |
| `aml_sdc_burn.ini` | SD burning descriptor | 602 | `e8d93f46803e26ce8457f902edd348806cbb88c8ef706deea2eb441879f0a5cc` |
| `logo.PARTITION` | boot logo | 1 338 688 | `9ec538e55aba518905a17741dade6ad626f2af7a17dc89a6380313c139e6239b` |

`fw_builder` copies `bootloader.img` to `DDR.USB`, `UBOOT.USB` and
`bootloader.PARTITION`, and `u-boot.bin.sd.bin` to `aml_sdc_burn.UBOOT`.

Check them with:

```sh
cd device/yandex/station_max/upgrade
sha256sum bootloader.img u-boot.bin.sd.bin platform.conf aml_sdc_burn.ini logo.PARTITION
```
