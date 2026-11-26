"""Cross-profile emulator and real FAT boot-script assembly without hardware."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def shell(code, env):
    return subprocess.run(['bash', '-euo', 'pipefail', '-c', code],
        env=dict(os.environ, **env), capture_output=True, text=True)


def library(name):
    return f'source "{ROOT}/lib/general.sh"; source "{ROOT}/lib/{name}"; '


@pytest.fixture
def emulators(tmp_path):
    binaries = tmp_path / 'bin'; binaries.mkdir()
    for name in ['qemu-aarch64-static', 'qemu-arm-static']:
        path = binaries / name
        path.write_text('#!/bin/sh\n# ' + name + '\nexit 0\n')
        path.chmod(0o755)
    return binaries


@pytest.mark.parametrize('architecture,emulator', [('arm64', 'qemu-aarch64-static'),
    ('armhf', 'qemu-arm-static'), ('armel', 'qemu-arm-static')])
def test_debian_architecture_selects_matching_emulator(architecture, emulator, emulators):
    result = shell(library('rootfs-debian.sh') + 'resolve_rootfs_qemu_binary "$ARCH"',
        {'ARCH': architecture, 'ROOTFS_QEMU_BINARY': '',
         'PATH': str(emulators) + os.pathsep + os.environ['PATH']})
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == str(emulators / emulator)


def test_explicit_reviewed_emulator_path_supports_other_architectures(tmp_path):
    emulator = tmp_path / 'tools/reviewed-static'
    emulator.parent.mkdir(); emulator.write_text('#!/bin/sh\nexit 0\n'); emulator.chmod(0o755)
    result = shell(library('rootfs-debian.sh') + 'resolve_rootfs_qemu_binary riscv64',
        {'PROJECT_ROOT': str(tmp_path), 'ROOTFS_QEMU_BINARY': 'tools/reviewed-static'})
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == str(emulator)


@pytest.mark.parametrize('kind', ['missing', 'empty', 'not-executable', 'directory', 'symlink'])
def test_invalid_emulator_is_rejected_before_base_or_staging_mutation(tmp_path, kind):
    emulator = tmp_path / 'emulator'
    if kind == 'empty': emulator.touch(); emulator.chmod(0o755)
    if kind == 'not-executable': emulator.write_text('data'); emulator.chmod(0o644)
    if kind == 'directory': emulator.mkdir()
    if kind == 'symlink':
        actual = tmp_path / 'actual'; actual.write_text('#!/bin/sh\n'); actual.chmod(0o755)
        emulator.symlink_to(actual)
    cache = tmp_path / 'cache/rootfs_debian_base'; cache.mkdir(parents=True)
    (cache / 'previous').write_text('untouched')
    command = library('rootfs-debian.sh') + 'check_tool() { :; }; build_debian_micro_rootfs'
    result = shell(command, {'PROJECT_ROOT': str(tmp_path), 'ARCH': 'armhf',
        'ROOTFS_QEMU_BINARY': str(emulator), 'OS_NAME': 'AtriOS', 'RELEASE_NAME': 'test'})
    assert result.returncode != 0
    assert 'ROOTFS_QEMU_BINARY' in result.stdout
    assert (cache / 'previous').read_text() == 'untouched'
    assert not (tmp_path / 'output').exists()


def test_unknown_debian_architecture_requires_an_explicit_emulator(tmp_path):
    result = shell(library('rootfs-debian.sh') + 'resolve_rootfs_qemu_binary riscv64',
        {'ROOTFS_QEMU_BINARY': ''})
    assert result.returncode != 0
    assert 'Set ROOTFS_QEMU_BINARY' in result.stdout


def test_emulator_content_and_basename_are_part_of_base_cache_profile(tmp_path, emulators):
    keyring = tmp_path / 'archive.gpg'; keyring.write_bytes(b'reviewed archive keys')
    command = library('rootfs-debian.sh') + 'debian_base_profile bookworm arm64 https://mirror.test python3 "$KEYRING" "$QEMU"'
    env = {'KEYRING': str(keyring), 'QEMU': str(emulators / 'qemu-aarch64-static')}
    first = shell(command, env)
    assert first.returncode == 0, first.stderr
    assert 'qemu=qemu-aarch64-static' in first.stdout
    assert hashlib.sha256((emulators / 'qemu-aarch64-static').read_bytes()).hexdigest() in first.stdout
    (emulators / 'qemu-aarch64-static').write_text('#!/bin/sh\n# updated reviewed emulator\nexit 0\n')
    second = shell(command, env)
    assert second.returncode == 0, second.stderr
    assert second.stdout != first.stdout


@pytest.mark.parametrize('architecture,override,expected', [('arm64', '', 'booti'),
    ('arm', '', 'bootz'), ('arm', 'bootm', 'bootm'), ('riscv', 'booti', 'booti')])
def test_kernel_boot_command_defaults_and_reviewed_override(architecture, override, expected):
    result = shell(library('image-emmc.sh') + 'resolve_uboot_kernel_boot_command',
        {'KERNEL_BUILD_ARCH': architecture, 'UBOOT_KERNEL_BOOT_COMMAND': override})
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == expected


@pytest.mark.parametrize('architecture,override', [('riscv', ''), ('arm64', 'sh'),
    ('arm', 'bootz; reset'), ('arm64', 'booti\nreset'), ('arm', '../bootm')])
def test_invalid_boot_command_fails_before_image_output(tmp_path, architecture, override):
    result = shell(library('image-emmc.sh') + 'build_emmc_image',
        {'PROJECT_ROOT': str(tmp_path), 'KERNEL_BUILD_ARCH': architecture,
         'UBOOT_KERNEL_BOOT_COMMAND': override})
    assert result.returncode != 0
    assert 'UBOOT_KERNEL_BOOT_COMMAND' in result.stdout
    assert not (tmp_path / 'output').exists()


@pytest.mark.parametrize('debian,architecture,override,expected', [
    ('arm64', 'arm64', '', 'booti'), ('armhf', 'arm', '', 'bootz'),
    ('armel', 'arm', 'bootm', 'bootm')])
def test_real_bounded_fat_image_contains_the_selected_boot_command(tmp_path, debian, architecture, override, expected):
    for tool in ['truncate', 'mkfs.vfat', 'mcopy', 'sfdisk', 'dd', 'mkimage']:
        if not shutil.which(tool): pytest.skip('Image tool unavailable: ' + tool)
    artifacts = {}
    for name, data in [('bootloader', bytes(range(256)) * 16), ('kernel', b'profile-kernel'),
                       ('dtb', b'profile-dtb'), ('rootfs', b'profile-rootfs')]:
        path = tmp_path / name; path.write_bytes(data); artifacts[name] = path
    env = {'PROJECT_ROOT': str(tmp_path), 'IMAGE_FILENAME': 'speaker.img',
        'BOOTLOADER_BIN': str(artifacts['bootloader']), 'KERNEL_IMAGE': str(artifacts['kernel']),
        'DTB_IMAGE': str(artifacts['dtb']), 'ROOTFS_IMAGE': str(artifacts['rootfs']),
        'ARCH': debian, 'KERNEL_BUILD_ARCH': architecture, 'UBOOT_KERNEL_BOOT_COMMAND': override,
        'DTS_NAME': 'custom-speaker', 'OS_NAME': 'AtriOS', 'RELEASE_NAME': 'test',
        'SERIAL_CONSOLE': 'ttyS2,1500000', 'BOOT_PARTITION_START': '2048',
        'BOOT_PARTITION_SIZE': '131072', 'ROOTFS_PARTITION_START': '133120',
        'ROOTFS_PARTITION_SIZE': '2048'}
    result = shell(library('image-emmc.sh') + 'build_emmc_image', env)
    assert result.returncode == 0, result.stdout + result.stderr
    image = tmp_path / 'output/images/speaker.img'
    extracted = tmp_path / 'boot.cmd'
    readback = subprocess.run(['mcopy', '-i', str(image) + '@@1048576', '::/boot.cmd', str(extracted)],
        capture_output=True, text=True)
    assert readback.returncode == 0, readback.stderr
    boot = extracted.read_text()
    assert '\n            ' + expected + ' ${kernel_addr_r} - ${fdt_addr_r}\n' in boot
    assert 'console=ttyS2,1500000' in boot
    extracted_kernel = tmp_path / 'extracted-kernel'
    subprocess.run(['mcopy', '-i', str(image) + '@@1048576', '::/Image', str(extracted_kernel)], check=True)
    assert extracted_kernel.read_bytes() == artifacts['kernel'].read_bytes()