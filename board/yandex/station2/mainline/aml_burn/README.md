# AML recovery gadget

This gadget supports bounded, block-aligned `download store <partition> normal <hex-size>` transfers to existing MMC partitions. It does not implement a complete USB Burning Tool erase/partition/verify/sparse/finalization session. Unsupported commands return failure; do not advertise a successful full burn based on Python model tests.

Select the target explicitly before `aml_burn`: `setenv aml_mmc_dev 1` (use the device number shown by `mmc list` for the intended eMMC). The gadget always selects the MMC user hardware partition. GPT partition names are resolved through U-Boot. For the generated DOS image only, `boot` maps to partition 1 and `rootfs` to partition 2. Raw bootloader writes are intentionally unsupported.

Every chunk must be nonzero, at most 1 MiB, a whole number of device blocks, fit the declared partition transfer, use algorithm NONE or ADDSUM, and request a 512-byte acknowledgement. After the first successfully written chunk, sequence numbers must increase by one. Failed checksums may be retried without advancing the offset. Never retry an acknowledged write with the previous sequence.

Tests under `tests/native` exercise the production C wire layout/checksum under ASan/UBSan. Python framing scenarios do not prove USB enumeration, physical writes, DDR training, or boot recovery. The current boot layout has no automatic boot-failure/A/B rollback contract.
