"""Global pytest configuration and fixtures for MainlineAtriOS E2E tests."""

import os
import sys
from pathlib import Path
import pytest

# Add tests directory to python path
TESTS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TESTS_DIR.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
if str(TESTS_DIR) not in sys.path:
    sys.path.insert(0, str(TESTS_DIR))


def pytest_addoption(parser):
    """Add custom command-line options for selective milestone, tier, and requirement execution."""
    parser.addoption(
        "--milestone",
        action="store",
        default="all",
        help="Run tests up to or matching specific milestone (e.g. M0, M1, M2, M3, M4, M5, or all)",
    )
    parser.addoption(
        "--tier",
        action="store",
        default="all",
        help="Run tests belonging to a specific tier (1, 2, 3, 4, or all)",
    )
    parser.addoption(
        "--feature",
        action="store",
        default="all",
        help="Run tests targeting a specific feature (e.g. F00, F01, ..., F17, or all)",
    )
    parser.addoption(
        "--requirement",
        action="store",
        default="all",
        help="Run tests targeting a specific requirement (R1, R2, R3, R4, or all)",
    )


def pytest_configure(config):
    """Register custom markers."""
    config.addinivalue_line("markers", "tier1: Tier 1 Baseline Feature Coverage")
    config.addinivalue_line("markers", "tier2: Tier 2 Boundary & Corner Cases")
    config.addinivalue_line("markers", "tier3: Tier 3 Cross-Feature Combinations")
    config.addinivalue_line("markers", "tier4: Tier 4 Real-World Application Scenarios")
    config.addinivalue_line("markers", "feature(id): Target feature ID (F00-F17)")
    config.addinivalue_line("markers", "milestone(id): Milestone ID (M0-M5)")
    config.addinivalue_line("markers", "requirement(id): Target requirement ID (R1-R4)")
    config.addinivalue_line("markers", "aml_burn: AML burn protocol and recovery tests")
    config.addinivalue_line("markers", "cross_feature: Cross-feature combination tests")


def pytest_collection_modifyitems(config, items):
    """Filter tests according to --milestone, --tier, --feature, and --requirement flags."""
    selected_milestone = config.getoption("--milestone")
    selected_tier = config.getoption("--tier")
    selected_feature = config.getoption("--feature")
    selected_requirement = config.getoption("--requirement")

    # Milestone dependency hierarchy
    milestone_order = {
        "M0": ["M0"],
        "M1": ["M0", "M1"],
        "M2": ["M0", "M1", "M2"],
        "M3": ["M0", "M1", "M2", "M3"],
        "M4": ["M0", "M1", "M2", "M3", "M4"],
        "M5": ["M0", "M1", "M2", "M3", "M4", "M5"],
    }

    filtered_items = []
    deselected_items = []

    for item in items:
        # Check tier
        if selected_tier != "all":
            tier_marker = f"tier{selected_tier}"
            if not item.get_closest_marker(tier_marker):
                deselected_items.append(item)
                continue

        # Check feature
        if selected_feature != "all":
            feat_marker = item.get_closest_marker("feature")
            if not feat_marker or feat_marker.args[0] != selected_feature:
                deselected_items.append(item)
                continue

        # Check requirement
        if selected_requirement != "all":
            req_marker = item.get_closest_marker("requirement")
            if not req_marker or req_marker.args[0].upper() != selected_requirement.upper():
                deselected_items.append(item)
                continue

        # Check milestone
        if selected_milestone != "all":
            ms_marker = item.get_closest_marker("milestone")
            if ms_marker:
                item_ms = ms_marker.args[0]
                allowed_ms = milestone_order.get(selected_milestone, [selected_milestone])
                if item_ms not in allowed_ms:
                    deselected_items.append(item)
                    continue

        filtered_items.append(item)

    if deselected_items:
        config.hook.pytest_deselected(items=deselected_items)
        items[:] = filtered_items


# Fixtures for filesystem paths and assets
@pytest.fixture(scope="session")
def repo_root() -> Path:
    return REPO_ROOT


@pytest.fixture(scope="session")
def drivers_dir(repo_root: Path) -> Path:
    return repo_root / "drivers" / "atrios"


@pytest.fixture(scope="session")
def dts_file(repo_root: Path) -> Path:
    return repo_root / "board" / "yandex" / "station2" / "mainline" / "dts" / "meson-sm1-yandex-station-max.dts"


@pytest.fixture(scope="session")
def kernel_defconfig(repo_root: Path) -> Path:
    return repo_root / "board" / "yandex" / "station2" / "mainline" / "linux_7.0_station_max_defconfig"


@pytest.fixture(scope="session")
def uboot_defconfig(repo_root: Path) -> Path:
    return repo_root / "board" / "yandex" / "station2" / "mainline" / "uboot_station_max_defconfig"


@pytest.fixture(scope="session")
def rootfs_overlay(repo_root: Path) -> Path:
    return repo_root / "board" / "yandex" / "station2" / "rootfs_overlay"


@pytest.fixture(scope="session")
def uboot_script(repo_root: Path) -> Path:
    return repo_root / "scripts" / "build_mainline_uboot.sh"


@pytest.fixture(scope="session")
def kernel_tree() -> Path:
    return Path("/home/lmods/linux-leftymods")


@pytest.fixture(scope="session")
def fip_dir(repo_root: Path) -> Path:
    return repo_root / "bootloader" / "uboot-repo"


@pytest.fixture(scope="session")
def kitchen_dir() -> Path:
    return Path("/home/lmods/AmlogicKitchen")


@pytest.fixture(scope="session")
def upgrade_dir(repo_root: Path) -> Path:
    return repo_root / "device" / "yandex" / "station_max" / "upgrade"


@pytest.fixture(scope="session")
def scripts_dir(repo_root: Path) -> Path:
    return repo_root / "scripts"
