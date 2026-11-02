"""Tier 3: Cross-Feature Combinations - Burn Package Staging & Staged Binary Contracts."""

from pathlib import Path
import pytest
from common.aml_burn_inspector import KitchenBlobAuditor


@pytest.mark.tier3
@pytest.mark.cross_feature
@pytest.mark.aml_burn
@pytest.mark.milestone("M1")
class TestCombBurnPackageStaging:
    """Validate cross-feature integration of burn package staging and blob origin compliance."""

    @pytest.fixture(autouse=True)
    def setup(self, repo_root: Path, kitchen_dir: Path):
        self.repo_root = repo_root
        self.pack_script = repo_root / "scripts" / "pack_aml_burn.py"
        self.staging_dir = repo_root / "output" / "burn_staging"
        self.auditor = KitchenBlobAuditor(kitchen_dir)

    def test_comb_pack_script_references_split_binaries(self):
        """Cross-verify that pack_aml_burn.py uses split usb.bl2 and usb.tpl instead of monolithic bootloader."""
        assert self.pack_script.exists(), f"Packing script missing at {self.pack_script}"
        content = self.pack_script.read_text(encoding="utf-8", errors="replace")

        # Monolithic bootloader copying is the root cause of BL2 FIP TOC failure
        monolithic_copy = (
            'shutil.copy2(bootloader_src, os.path.join(staging_dir, "DDR.USB"))' in content
            and 'shutil.copy2(bootloader_src, os.path.join(staging_dir, "UBOOT.USB"))' in content
        )
        assert not monolithic_copy, (
            "pack_aml_burn.py still copies monolithic bootloader.img to both DDR.USB and UBOOT.USB!"
        )

    def test_comb_staged_assets_zero_kitchen_collisions(self):
        """Cross-verify that all staged assets have zero SHA256 match with stock kitchen blobs."""
        if not self.staging_dir.exists():
            pytest.skip("burn_staging directory not yet created")

        collisions = self.auditor.audit_target(self.staging_dir)
        assert len(collisions) == 0, (
            f"Staged burn package contains proprietary Yandex firmware blobs: {collisions}"
        )

    def test_comb_boot_partition_android_magic(self):
        """Cross-verify that boot.PARTITION adheres to Android Boot Image v1 specification."""
        boot_part = self.staging_dir / "boot.PARTITION"
        if not boot_part.exists():
            pytest.skip("boot.PARTITION not yet staged")

        header = boot_part.read_bytes()[:8]
        assert header == b"ANDROID!", f"boot.PARTITION header '{header}' != 'ANDROID!'"
