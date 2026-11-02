"""Tier 1: Feature Coverage - Requirement R3: Cleanroom Origin & License Verification."""

from pathlib import Path
import pytest
from common.aml_burn_inspector import KitchenBlobAuditor


@pytest.mark.tier1
@pytest.mark.requirement("R3")
@pytest.mark.aml_burn
@pytest.mark.milestone("M1")
class TestR3CleanroomLicense:
    """Forensic verification prohibiting proprietary binaries from original Yandex firmware (R3)."""

    @pytest.fixture(autouse=True)
    def setup(self, kitchen_dir: Path, repo_root: Path):
        self.auditor = KitchenBlobAuditor(kitchen_dir)
        self.repo_root = repo_root

    def test_r3_zero_sha256_match_with_kitchen_level1(self, fip_dir: Path):
        """Verify zero SHA256 collision between active FIP blobs and stock AmlogicKitchen blobs."""
        stock_hashes = self.auditor.get_stock_hashes()
        assert len(stock_hashes) > 0, "AmlogicKitchen stock blobs must be present to establish ground truth"

        collisions = self.auditor.audit_target(fip_dir / "fip" / "g12a")
        assert len(collisions) == 0, (
            f"Forensic violation: {len(collisions)} blobs in fip/g12a match proprietary Yandex firmware: {collisions}"
        )

    def test_r3_ddr_usb_zero_match_with_stock(self, repo_root: Path):
        """Verify staged or generated DDR.USB does NOT match stock DDR.USB / DDR_ENC.USB."""
        stock_hashes = self.auditor.get_stock_hashes()
        stock_ddr_hash = stock_hashes.get("DDR.USB")
        stock_ddr_enc_hash = stock_hashes.get("DDR_ENC.USB")

        # Scan for any DDR.USB in relevant directories (avoiding massive buildroot tree)
        search_dirs = [repo_root / "output", repo_root / "device", repo_root / "bootloader", repo_root / "scripts"]
        ddr_candidates = []
        for sdir in search_dirs:
            if sdir.exists():
                ddr_candidates.extend(list(sdir.rglob("DDR.USB")) + list(sdir.rglob("DDR_ENC.USB")))
        for ddr in ddr_candidates:
            h = self.auditor.sha256_file(ddr)
            assert h != stock_ddr_hash, f"DDR.USB at {ddr} matches proprietary stock DDR.USB ({h})!"
            assert h != stock_ddr_enc_hash, f"DDR.USB at {ddr} matches proprietary stock DDR_ENC.USB ({h})!"

    def test_r3_uboot_usb_zero_match_with_stock(self, repo_root: Path):
        """Verify staged or generated UBOOT.USB does NOT match stock UBOOT.USB / UBOOT_ENC.USB."""
        stock_hashes = self.auditor.get_stock_hashes()
        stock_uboot_hash = stock_hashes.get("UBOOT.USB")
        stock_uboot_enc_hash = stock_hashes.get("UBOOT_ENC.USB")

        search_dirs = [repo_root / "output", repo_root / "device", repo_root / "bootloader", repo_root / "scripts"]
        uboot_candidates = []
        for sdir in search_dirs:
            if sdir.exists():
                uboot_candidates.extend(list(sdir.rglob("UBOOT.USB")) + list(sdir.rglob("UBOOT_ENC.USB")))
        for ub in uboot_candidates:
            h = self.auditor.sha256_file(ub)
            assert h != stock_uboot_hash, f"UBOOT.USB at {ub} matches proprietary stock UBOOT.USB ({h})!"
            assert h != stock_uboot_enc_hash, f"UBOOT.USB at {ub} matches proprietary stock UBOOT_ENC.USB ({h})!"

    def test_r3_bootloader_partition_zero_match_with_stock(self, upgrade_dir: Path):
        """Verify target bootloader.img does NOT match stock bootloader.PARTITION."""
        stock_hashes = self.auditor.get_stock_hashes()
        stock_bootloader_hash = stock_hashes.get("bootloader.PARTITION")

        bootloader_img = upgrade_dir / "bootloader.img"
        if bootloader_img.exists():
            h = self.auditor.sha256_file(bootloader_img)
            assert h != stock_bootloader_hash, (
                f"bootloader.img ({h}) matches proprietary stock bootloader.PARTITION!"
            )

    def test_r3_scripts_do_not_embed_proprietary_payloads(self, scripts_dir: Path):
        """Verify build and packing scripts do not bundle or download proprietary firmware blobs."""
        for script in scripts_dir.glob("*.py"):
            text = script.read_text(encoding="utf-8", errors="replace")
            # Verify script does not reference level1 stock dumps
            if script.name not in ["build_aml_burn_package.py", "pack_aml_burn_sei610.py"]:
                assert "AmlogicKitchen/level1" not in text, (
                    f"Script {script.name} directly references proprietary AmlogicKitchen dumps!"
                )
