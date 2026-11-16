# Build regressions without a kernel build, root privileges or hardware writes.
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/fw_builder'))

def shell(code, *, env=None, cwd=None):
    return subprocess.run(['bash', '-euo', 'pipefail', '-c', code],
        env=dict(os.environ, **(env or {})), cwd=cwd, capture_output=True, text=True)

def library(name):
    return f'source "{ROOT}/lib/general.sh"; source "{ROOT}/lib/{name}"; '

@pytest.mark.parametrize('name', ['compile.sh', 'atrios', 'build_atrios.sh',
    'scripts/build_full_in_background.sh', 'scripts/build_kernel_and_image.sh',
    *[str(p.relative_to(ROOT)) for p in (ROOT / 'lib').glob('*.sh')]])
def test_build_shell_syntax(name):
    result = subprocess.run(['bash', '-n', str(ROOT / name)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr

def test_cli_info_outside_repo_and_unknown_option(tmp_path):
    result = subprocess.run(['bash', str(ROOT / 'atrios'), 'info'], cwd=tmp_path,
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert str(ROOT / 'output/images/rootfs-debian-micro.ext4') in result.stdout
    result = subprocess.run(['bash', str(ROOT / 'compile.sh'), '--typo'], cwd=tmp_path,
                            capture_output=True, text=True)
    assert result.returncode != 0
    assert 'Unknown argument' in result.stdout

@pytest.mark.parametrize('source,expected', [
    ('/dev/mmcblk1p2', '/dev/mmcblk1'), ('/dev/mmcblk10p2', '/dev/mmcblk10'),
    ('/dev/mmcblk0p1', None), ('/dev/mmcblk0foop2', None), ('/dev/sda2', None), ('overlay', None)])
def test_boot_target_comes_only_from_valid_root_partition(source, expected):
    result = shell(library('package-boot.sh') + 'atrios_root_disk "$SOURCE"', env={'SOURCE': source})
    assert (result.returncode == 0) == (expected is not None)
    if expected: assert result.stdout.strip() == expected

@pytest.mark.parametrize('size,start,success', [(512, 1, True), (513, 1, False),
    (0, 100, False), (4096, 8, True), (4097, 8, False), ('bad', 8, False)])
def test_bootloader_write_bounds(size, start, success):
    result = shell(library('package-boot.sh') + 'atrios_check_bootloader_size "$SIZE" "$START"',
                   env={'SIZE': str(size), 'START': str(start)})
    assert (result.returncode == 0) == success

@pytest.mark.parametrize('args,success', [
    ('4096 4096 16 32 48 8', True), ('8193 4096 16 32 48 8', False),
    ('4096 4097 16 32 48 8', False), ('4096 4096 16 33 48 8', False),
    ('4096 4096 0 32 48 8', False), ('4096 bad 16 32 48 8', False)])
def test_image_geometry_rejects_overwrite(args, success):
    result = shell(library('image-emmc.sh') + 'validate_image_geometry ' + args)
    assert (result.returncode == 0) == success

def test_kernel_patch_is_idempotent_and_rejects_incompatible_input(tmp_path):
    tree = tmp_path / 'tree'; tree.mkdir()
    source = tree / 'file.txt'; source.write_text('before\n')
    patch = tmp_path / 'change.patch'
    patch.write_text('--- a/file.txt\n+++ b/file.txt\n@@ -1 +1 @@\n-before\n+after\n')
    command = library('compilation-kernel.sh') + 'apply_kernel_patch "$TREE" "$PATCH"'
    env = {'TREE': str(tree), 'PATCH': str(patch)}
    assert shell(command, env=env).returncode == 0
    assert source.read_text() == 'after\n'
    assert shell(command, env=env).returncode == 0
    source.write_text('unexpected\n')
    assert shell(command, env=env).returncode != 0
    assert source.read_text() == 'unexpected\n'
    assert not (tree / 'file.txt.rej').exists()

@pytest.fixture
def generated_packages(tmp_path):
    project = tmp_path / 'project'
    (project / 'lib').mkdir(parents=True)
    shutil.copy(ROOT / 'lib/package-boot.sh', project / 'lib/package-boot.sh')
    (project / 'cache/kernel').mkdir(parents=True)
    (project / 'Image').write_bytes(b'kernel payload')
    (project / 'board.dtb').write_bytes(b'dtb payload')
    (project / 'u-boot.bin').write_bytes(b'u' * 4096)
    binaries = tmp_path / 'bin'; binaries.mkdir()
    # modules_install includes both board and non-board modules.
    (binaries / 'make').write_text('''#!/usr/bin/env python3
import pathlib,sys
if 'kernelrelease' in sys.argv:
 print('7.3.1-AtriOS-aml'); sys.exit(0)
assert 'modules_install' in sys.argv
path=pathlib.Path(next(x.split('=',1)[1] for x in sys.argv if x.startswith('INSTALL_MOD_PATH=')))
root=path/'lib/modules/7.3.1-AtriOS-aml'
for name in ['kernel/net/wifi.ko','kernel/drivers/atrios/screen.ko','modules.order','modules.builtin']:
 p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'module')
''')
    (binaries / 'depmod').write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$DEPMOD_LOG"\n')
    for p in binaries.iterdir(): p.chmod(0o755)
    env = {'PROJECT_ROOT': str(project), 'BOARD': 'yndxsttion53', 'ARCH': 'arm64',
           'CROSS_COMPILE': 'aarch64-linux-gnu-', 'BOARD_NAME': 'Yandex Station Max',
           'DTS_NAME': 'meson-sm1-yandex-station-max', 'KERNEL_IMAGE': str(project / 'Image'),
           'DTB_IMAGE': str(project / 'board.dtb'), 'BOOTLOADER_BIN': str(project / 'u-boot.bin'),
           'PATH': str(binaries) + os.pathsep + os.environ['PATH'],
           'DEPMOD_LOG': str(tmp_path / 'depmod.log')}
    result = shell(library('packages.sh') + 'build_u_boot_package; build_kernel_package', env=env)
    assert result.returncode == 0, result.stdout + result.stderr
    return project, tmp_path / 'depmod.log'

def package_tar(package, control=False):
    data = subprocess.check_output(['dpkg-deb', '--ctrl-tarfile' if control else '--fsys-tarfile', str(package)])
    return tarfile.open(fileobj=io.BytesIO(data))

def test_packages_embed_board_and_preserve_disk_signature(generated_packages):
    project, _ = generated_packages
    package = next((project / 'output/packages').glob('atrios-u-boot-*.deb'))
    script = package_tar(package, control=True).extractfile('./postinst').read().decode()
    assert "BOARD='yndxsttion53'" in script
    assert 'TARGET_DEV="/dev/mmcblk0"' not in script
    assert 'count=440 ' in script and 'count=442 ' not in script
    assert subprocess.run(['sh', '-n'], input=script, text=True).returncode == 0
    # Image-build mode must exit before hardware detection or writes.
    result = subprocess.run(['sh'], input=script, capture_output=True, text=True,
                            env=dict(os.environ, ATRIOS_IMAGE_BUILD='1'))
    assert result.returncode == 0, result.stderr

def test_kernel_package_has_full_release_modules_and_payload(generated_packages):
    project, log = generated_packages
    package = next((project / 'output/packages').glob('atrios-linux-image-*.deb'))
    contents = package_tar(package)
    for name in ['./usr/lib/modules/7.3.1-AtriOS-aml/kernel/net/wifi.ko',
                 './usr/lib/modules/7.3.1-AtriOS-aml/kernel/drivers/atrios/screen.ko',
                 './usr/lib/atrios-kernel/7.3.1-AtriOS-aml/Image']:
        assert name in contents.getnames()
    assert all(m.uid == m.gid == 0 for m in contents.getmembers())
    script = package_tar(package, control=True).extractfile('./postinst').read().decode()
    assert "DTS_NAME='meson-sm1-yandex-station-max'" in script
    assert 'depmod -a "$KERNEL_RELEASE"' in script
    assert '7.3.1-AtriOS-aml' in log.read_text()
    assert subprocess.run(['sh', '-n'], input=script, text=True).returncode == 0

def test_legacy_missing_packer_does_not_make_mock_firmware(tmp_path, monkeypatch):
    from builder import repack
    monkeypatch.setattr(repack, '_detect_workspace', lambda: tmp_path)
    monkeypatch.setattr(repack, 'check_dirty_state', lambda *a, **kw: (True, {}, {}))
    monkeypatch.setattr(repack, '_find_ampack', lambda: None)
    code, message = repack.build_image(version_num=1)
    assert code != 0 and 'ampack' in message
    assert not list(tmp_path.glob('*.img'))
    assert not (tmp_path / 'staging').exists()

def test_legacy_missing_rootfs_fails_instead_of_placeholder(tmp_path, monkeypatch):
    from builder import nobase
    monkeypatch.setattr(nobase, 'DEFAULT_SYSTEM_TREE', tmp_path / 'missing')
    monkeypatch.setattr(nobase, '_find_make_ext4fs', lambda: None)
    staging = tmp_path / 'staging'; staging.mkdir()
    with pytest.raises(RuntimeError, match='placeholders are forbidden'):
        nobase._build_system_partition(staging, tmp_path / 'work')
    assert not (staging / 'system.PARTITION').exists()


def test_image_target_rebuilds_packages_and_rootfs_despite_stale_files(tmp_path):
    project = tmp_path / 'project'; (project / 'lib').mkdir(parents=True)
    shutil.copy(ROOT / 'compile.sh', project / 'compile.sh')
    for name, body in {
        'general.sh': 'exit_with_error() { echo "$*"; exit 1; }\n',
        'config.sh': 'load_board_config() { :; }\n',
        'compilation-kernel.sh': '',
        'packages.sh': 'build_all_packages() { echo packages >> "$ORDER"; }\n',
        'rootfs-debian.sh': 'build_debian_micro_rootfs() { echo rootfs >> "$ORDER"; }\n',
        'image-emmc.sh': 'build_emmc_image() { echo image >> "$ORDER"; }\n',
    }.items(): (project / 'lib' / name).write_text(body)
    (project / 'output/images').mkdir(parents=True)
    (project / 'output/packages').mkdir(parents=True)
    (project / 'output/images/rootfs-debian-micro.ext4').write_bytes(b'stale')
    (project / 'output/packages/atrios-u-boot-yndxsttion53_1.0_arm64.deb').write_bytes(b'stale')
    (project / 'output/packages/atrios-linux-image-7.3.1_7.3.1-1_arm64.deb').write_bytes(b'stale')
    log = tmp_path / 'order'
    result = subprocess.run(['bash', str(project / 'compile.sh'), 'TARGET=image'],
        env=dict(os.environ, ORDER=str(log)), capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert log.read_text().splitlines() == ['packages', 'rootfs', 'image']


def test_real_file_image_assembly_preserves_mbr_and_embeds_current_payload(tmp_path):
    import struct
    tools = ['mkfs.vfat', 'mcopy', 'sfdisk', 'mkimage']
    if any(shutil.which(tool) is None for tool in tools):
        pytest.skip('Optional image tools not installed')
    # All destinations are regular temporary files, never block devices.
    bootloader = tmp_path / 'bootloader.bin'
    data = bytes(range(256)) * 16
    bootloader.write_bytes(data)
    kernel = tmp_path / 'Image'; kernel.write_bytes(b'current-kernel')
    dtb = tmp_path / 'board.dtb'; dtb.write_bytes(b'current-dtb')
    rootfs = tmp_path / 'rootfs.ext4'; rootfs.write_bytes(b'current-rootfs')
    env = {'PROJECT_ROOT': str(tmp_path), 'IMAGE_FILENAME': 'test.img',
        'BOOTLOADER_BIN': str(bootloader), 'KERNEL_IMAGE': str(kernel),
        'DTB_IMAGE': str(dtb), 'ROOTFS_IMAGE': str(rootfs),
        'DTS_NAME': 'board', 'OS_NAME': 'AtriOS', 'RELEASE_NAME': 'test',
        'SERIAL_CONSOLE': 'ttyAML0,115200',
        'BOOT_PARTITION_START': '2048', 'BOOT_PARTITION_SIZE': '131072',
        'ROOTFS_PARTITION_START': '133120', 'ROOTFS_PARTITION_SIZE': '2048'}
    result = shell(library('image-emmc.sh') + 'build_emmc_image', env=env)
    assert result.returncode == 0, result.stdout + result.stderr
    image = tmp_path / 'output/images/test.img'
    with image.open('rb') as f:
        mbr = f.read(512)
        assert mbr[:440] == data[:440]
        assert struct.unpack_from('<I', mbr, 440)[0] == 0x4a7e8b12
        assert mbr[510:512] == b'\x55\xaa'
        f.seek(512); assert f.read(4096-512) == data[512:]
        f.seek(133120*512); assert f.read(len(b'current-rootfs')) == b'current-rootfs'
    fat = tmp_path / 'fat.img'
    with image.open('rb') as f, fat.open('wb') as target:
        f.seek(2048*512); target.write(f.read(131072*512))
    extracted = tmp_path / 'extracted-kernel'
    subprocess.run(['mcopy', '-i', str(fat), '::/Image', str(extracted)], check=True)
    assert extracted.read_bytes() == b'current-kernel'
    assert not list((tmp_path / 'output/images').glob('.emmc-build.*'))


def test_kernel_integration_preserves_foreign_files_and_is_idempotent(tmp_path):
    import importlib.util
    spec = importlib.util.spec_from_file_location('kernel_tree_integration', ROOT / 'lib/kernel-tree.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    kernel = tmp_path / 'kernel'
    for relative in ['drivers/Kconfig', 'drivers/Makefile', 'arch/arm64/boot/dts/amlogic/Makefile']:
        p = kernel / relative; p.parent.mkdir(parents=True, exist_ok=True); p.write_text('# existing user rules\n')
    target = kernel / 'drivers/atrios'; target.mkdir()
    (target / 'driver.c').write_text('user modifications\n')
    (target / 'foreign.c').write_text('unrelated source\n')
    (kernel / '.config').write_text('CONFIG_USER_SETTING=y\n')
    drivers = tmp_path / 'drivers'; drivers.mkdir()
    (drivers / 'driver.c').write_text('current fixed driver\n')
    (drivers / 'Makefile').write_text('obj-y += driver.o\n')
    (drivers / 'Kconfig').write_text('config DRIVER\n')
    (drivers / 'stale.ko').write_bytes(b'not a source')
    dts = tmp_path / 'board.dts'; dts.write_text('/dts-v1/;\n')
    config = tmp_path / 'defconfig'; config.write_text('CONFIG_BOARD=y\n')
    module.integrate(kernel, drivers, dts, config)
    module.integrate(kernel, drivers, dts, config)
    assert (target / 'driver.c').read_text() == 'current fixed driver\n'
    assert (target / 'foreign.c').read_text() == 'unrelated source\n'
    assert not (target / 'stale.ko').exists()
    assert (kernel / 'drivers/Kconfig').read_text().count('source "drivers/atrios/Kconfig"') == 1
    assert (kernel / 'drivers/Makefile').read_text().count('obj-y += atrios/') == 1
    assert (kernel / 'arch/arm64/boot/dts/amlogic/Makefile').read_text().count('board.dtb') == 1
    backups = list((kernel / '.atrios-source-backups').rglob('*'))
    data = [p.read_text() for p in backups if p.is_file()]
    assert 'user modifications\n' in data
    assert 'CONFIG_USER_SETTING=y\n' in data


def test_kernel_integration_refuses_foreign_symlink(tmp_path):
    import importlib.util
    spec = importlib.util.spec_from_file_location('kernel_tree_integration', ROOT / 'lib/kernel-tree.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    kernel = tmp_path / 'kernel'; (kernel / 'drivers').mkdir(parents=True)
    foreign = tmp_path / 'foreign'; foreign.mkdir()
    (kernel / 'drivers/atrios').symlink_to(foreign)
    source = tmp_path / 'source'; source.mkdir()
    with pytest.raises(RuntimeError, match='foreign drivers symlink'):
        module.integrate(kernel, source, tmp_path / 'board.dts', tmp_path / 'defconfig')
    assert not list(foreign.iterdir())


def test_legacy_cached_mock_cannot_return_success(tmp_path, monkeypatch):
    from builder import repack
    (tmp_path / 'yandex_s905x3_custom_v1.img').write_bytes(b'AML_IMAGE_V2' + bytes(2036))
    monkeypatch.setattr(repack, '_detect_workspace', lambda: tmp_path)
    monkeypatch.setattr(repack, 'check_dirty_state', lambda *a, **kw: (False, {}, {}))
    monkeypatch.setattr(repack, '_find_ampack', lambda: None)
    code, message = repack.build_image(version_num=1)
    assert code != 0 and 'ampack' in message


def test_rootfs_restores_command_modes_without_executing_configuration(tmp_path):
    stage = tmp_path / 'stage'
    names = ['usr/bin/fastfetch', 'usr/sbin/daemon', 'usr/lib/atrios/ota', 'etc/init.d/S60atrios', 'etc/atrios/ota-public.pem']
    for name in names:
        path = stage / name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('fixture'); path.chmod(0o644)
    binaries = tmp_path / 'bin'; binaries.mkdir()
    (binaries / 'sudo').write_text('#!/bin/sh\nexec "$@"\n')
    (binaries / 'sudo').chmod(0o755)
    result = shell(library('rootfs-debian.sh') + 'prepare_rootfs_command_modes "$STAGE"',
                   env={'STAGE': str(stage), 'PATH': str(binaries) + os.pathsep + os.environ['PATH']})
    assert result.returncode == 0, result.stderr
    for name in names[:-1]: assert (stage / name).stat().st_mode & 0o111 == 0o111
    assert (stage / names[-1]).stat().st_mode & 0o111 == 0


@pytest.mark.parametrize('source,canonical,devno,sysfs,expected', [
    ('/dev/mmcblk1p2', '/dev/mmcblk1p2', '179:34', '/sys/devices/block/mmcblk1/mmcblk1p2', '/dev/mmcblk1p2'),
    ('/dev/root', '/dev/root', '179:34', '/sys/devices/block/mmcblk1/mmcblk1p2', '/dev/mmcblk1p2'),
    ('/dev/root', '', '179:34', '/sys/devices/block/mmcblk1/mmcblk1p2', '/dev/mmcblk1p2'),
    ('/dev/root', '', '8:2', '/sys/devices/block/sda/sda2', None),
    ('/dev/root', '', '../179:34', '/sys/devices/block/mmcblk1/mmcblk1p2', None),
    ('/dev/root', '', '179:34:1', '/sys/devices/block/mmcblk1/mmcblk1p2', None),
    ('/dev/root', '', '179:34', '/sys/devices/block/mmcblk1/mmcblk1p1', None),
])
def test_package_root_alias_resolves_using_kernel_device_number(source, canonical, devno, sysfs, expected):
    # Mock only read-only kernel inspection commands; no physical devices needed.
    script = library('package-boot.sh') + r'''
findmnt() { case "$*" in *MAJ:MIN*) printf '%s\n' "$DEVNO" ;; *) printf '%s\n' "$SOURCE" ;; esac; }
readlink() { case "$2" in /sys/dev/block/*) printf '%s\n' "$SYSFS" ;; *) printf '%s\n' "$CANONICAL" ;; esac; }
atrios_resolve_root_source
'''
    result = shell(script, env={'SOURCE': source, 'CANONICAL': canonical, 'DEVNO': devno, 'SYSFS': sysfs})
    assert (result.returncode == 0) == (expected is not None)
    if expected is not None: assert result.stdout.strip() == expected
