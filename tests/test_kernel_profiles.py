"""Per-board kernel isolation using tiny source trees and actual make/patch."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

import pytest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('kernel_profiles_under_test', ROOT / 'lib/kernel-profile.py')
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)

MAKEFILE = r'''.PHONY: olddefconfig Image zImage dtbs modules kernelrelease modules_install
olddefconfig:
	@printf '%s\n' '$(ARCH)' > built-arch
Image zImage:
	@mkdir -p arch/$(ARCH)/boot
	@cat marker > arch/$(ARCH)/boot/$@
dtbs:
	@find arch/$(ARCH)/boot/dts -name '*.dts' -exec sh -c 'cp "$$1" "$${1%.dts}.dtb"' sh {} \;
modules:
	@:
kernelrelease:
	@printf '%s\n' 7.3.1-profile-test
modules_install:
	@mkdir -p $(INSTALL_MOD_PATH)/lib/modules/7.3.1-profile-test/kernel
	@cp marker $(INSTALL_MOD_PATH)/lib/modules/7.3.1-profile-test/kernel/selected.ko
	@touch $(INSTALL_MOD_PATH)/lib/modules/7.3.1-profile-test/modules.order $(INSTALL_MOD_PATH)/lib/modules/7.3.1-profile-test/modules.builtin
'''


def write(root, name, data):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data)
    return path


def source_tree(destination):
    write(destination, 'Makefile', MAKEFILE)
    write(destination, 'marker', 'base\n')
    write(destination, 'drivers/Kconfig', '# upstream Kconfig\n')
    write(destination, 'drivers/Makefile', '# upstream Makefile\n')
    for arch in ('arm64', 'arm'):
        for vendor in ('rockchip', 'allwinner', 'amlogic', ''):
            write(destination, f'arch/{arch}/boot/dts/{vendor}/Makefile', '# upstream DTBs\n')
    return destination


@pytest.fixture
def project(tmp_path, monkeypatch):
    root = tmp_path / 'project'
    for name in ['kernel-profile.py', 'kernel-source.py', 'kernel-tree.py', 'compilation-kernel.sh', 'packages.sh', 'package-boot.sh', 'config.sh', 'general.sh']:
        dst = root / 'lib' / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'lib' / name, dst)
    write(root, 'drivers/atrios/Kconfig', 'config TEST_DRIVER\n')
    write(root, 'drivers/atrios/Makefile', 'obj-$(CONFIG_TEST_DRIVER) += test.o\n')
    write(root, 'drivers/atrios/test.c', '/* selected repository driver */\n')
    calls = []

    def fetch(url, sha, destination):
        calls.append((url, sha))
        source_tree(destination)
        write(destination, 'selected-pin', sha)
        return destination

    monkeypatch.setattr(PROFILE.SOURCE, 'fetch_kernel', fetch)
    return root, calls


def board(root, name='speaker-a', *, vendor='rockchip', arch='arm64', pin='a', patch='A'):
    base = 'board/' + name
    write(root, base + '/defconfig', 'CONFIG_' + name.upper().replace('-', '_') + '=y\n')
    write(root, base + '/dts/' + name + '.dts', '/dts-v1/; / { model = "' + name + '"; };\n')
    if patch:
        write(root, base + '/patches/linux-7.0/001-board.patch',
              '--- a/marker\n+++ b/marker\n@@ -1 +1 @@\n-base\n+' + patch + '\n')
    return {'BOARD': name, 'BOARD_NAME': name, 'ARCH': 'armhf' if arch == 'arm' else 'arm64',
            'KERNEL_ARCH': arch, 'BOARDFAMILY': 'fixture-family', 'CROSS_COMPILE': 'fixture-cross-',
            'KERNEL_DTS_SUBDIR': vendor, 'KERNEL_DTB_CONFIG': 'CONFIG_ARCH_' + (vendor.upper() or 'TEST'),
            'KERNEL_IMAGE_TARGET': 'zImage' if arch == 'arm' else 'Image',
            'UBOOT_SOURCE_DIR': base, 'KERNEL_DEFCONFIG': 'defconfig', 'DTS_NAME': name,
            'KERNEL_ARCHIVE_URL': 'https://example.invalid/linux-' + pin + '.tar.xz',
            'KERNEL_ARCHIVE_SHA256': pin * 64, 'PROJECT_ROOT': str(root), 'BUILD_JOBS': '1',
            'KERNEL_IMAGE': str(root / 'output' / (name + '.Image')),
            'DTB_IMAGE': str(root / 'output' / (name + '.dtb'))}


def shell(root, env, text):
    prefix = 'display_alert() { :; }; exit_with_error() { printf "%s\\n" "$*" >&2; exit 41; }; '
    prefix += 'source "$PROJECT_ROOT/lib/compilation-kernel.sh"; '
    return subprocess.run(['bash', '-euo', 'pipefail', '-c', prefix + text],
                          env={**os.environ, **env}, cwd=root, capture_output=True, text=True)


def prepare(root, env):
    profile = PROFILE.resolve_profile(root, env)
    PROFILE.prepare_profile(profile)
    return profile, Path(profile['build_dir'])


def test_two_pins_boards_patch_sets_and_vendors_build_in_isolated_trees(project):
    root, calls = project
    old = write(root, 'cache/kernel/Makefile', 'unrelated legacy local build\n')
    a = board(root)
    b = board(root, 'speaker-b', vendor='allwinner', pin='b', patch='B')
    pa, ka = prepare(root, a)
    pb, kb = prepare(root, b)
    for env in (a, b):
        result = shell(root, env, 'build_kernel; printf "PACKAGE_ARCH=%s\\n" "$ARCH"')
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'PACKAGE_ARCH=arm64' in result.stdout
    assert ka != kb and pa['fingerprint'] != pb['fingerprint']
    assert (ka / 'marker').read_text() == 'A\n'
    assert (kb / 'marker').read_text() == 'B\n'
    assert (ka / 'selected-pin').read_text() == 'a' * 64
    assert (kb / 'selected-pin').read_text() == 'b' * 64
    assert Path(a['KERNEL_IMAGE']).read_text() == 'A\n'
    assert Path(b['KERNEL_IMAGE']).read_text() == 'B\n'
    assert (ka / 'arch/arm64/boot/dts/rockchip/speaker-a.dtb').is_file()
    assert (kb / 'arch/arm64/boot/dts/allwinner/speaker-b.dtb').is_file()
    assert not (ka / 'arch/arm64/boot/dts/allwinner/speaker-b.dts').exists()
    assert not (kb / 'arch/arm64/boot/dts/rockchip/speaker-a.dts').exists()
    assert old.read_text() == 'unrelated legacy local build\n'
    assert len(calls) == 2
    PROFILE.prepare_profile(pa)
    assert len(calls) == 2


@pytest.mark.parametrize('changed', ['patch', 'config', 'dts', 'drivers', 'architecture', 'vendor', 'image', 'pin'])
def test_every_kernel_input_change_selects_a_new_cache(project, changed):
    root, _ = project
    env = board(root)
    before = PROFILE.resolve_profile(root, env)
    if changed == 'patch':
        write(root, 'board/speaker-a/patches/linux-7.0/001-board.patch', 'changed patch\n')
    elif changed == 'config':
        write(root, 'board/speaker-a/defconfig', 'CONFIG_NEW=y\n')
    elif changed == 'dts':
        write(root, 'board/speaker-a/dts/speaker-a.dts', '/dts-v1/; / {};\n')
    elif changed == 'drivers':
        write(root, 'drivers/atrios/test.c', '/* changed driver */\n')
    else:
        field, value = {'architecture': ('KERNEL_ARCH', 'arm'), 'vendor': ('KERNEL_DTS_SUBDIR', 'allwinner'),
                        'image': ('KERNEL_IMAGE_TARGET', 'Image.gz'), 'pin': ('KERNEL_ARCHIVE_SHA256', 'c' * 64)}[changed]
        env[field] = value
    after = PROFILE.resolve_profile(root, env)
    assert before['fingerprint'] != after['fingerprint']
    assert before['build_dir'] != after['build_dir']


def test_explicit_local_source_is_snapshotted_and_original_changes_survive(project, tmp_path):
    root, calls = project
    local = source_tree(tmp_path / 'my editable kernel')
    local.chmod(0o700)
    user = write(local, 'user-owned.c', 'user modifications\n')
    write(local, '.config', 'CONFIG_USER_PRESERVED=y\n')
    (local / 'internal-absolute-link').symlink_to(local / 'user-owned.c')
    (local / 'drivers/atrios').symlink_to(root / 'drivers/atrios', target_is_directory=True)
    env = board(root)
    env['KERNEL_SOURCE_DIR'] = str(local)
    # An unrelated existing shared cache must not defeat explicit source selection.
    write(root, 'cache/kernel/Makefile', 'other tree\n')
    profile, kernel = prepare(root, env)
    assert profile['identity']['source']['kind'] == 'local'
    assert not calls
    assert kernel != local and not kernel.is_symlink()
    assert kernel.stat().st_mode & 0o777 == 0o700
    assert (kernel / 'internal-absolute-link').resolve() == kernel / 'user-owned.c'
    assert not (kernel / 'drivers/atrios').is_symlink()
    result = shell(root, env, 'build_kernel')
    assert result.returncode == 0, result.stdout + result.stderr
    assert (local / 'marker').read_text() == 'base\n'
    assert (local / '.config').read_text() == 'CONFIG_USER_PRESERVED=y\n'
    assert user.read_text() == 'user modifications\n'
    user.write_text('later user changes\n')
    later = PROFILE.resolve_profile(root, env)
    assert later['fingerprint'] != profile['fingerprint']
    assert (kernel / 'user-owned.c').read_text() == 'user modifications\n'


@pytest.mark.parametrize('kind', ['foreign-directory', 'file', 'symlink'])
def test_foreign_cache_is_never_silently_reused_or_replaced(project, tmp_path, kind):
    root, calls = project
    env = board(root)
    profile = PROFILE.resolve_profile(root, env)
    destination = Path(profile['build_dir'])
    destination.parent.mkdir(parents=True)
    if kind == 'file':
        destination.write_text('unrelated file')
    elif kind == 'symlink':
        outside = tmp_path / 'foreign'
        write(outside, 'Makefile', 'unrelated')
        destination.symlink_to(outside, target_is_directory=True)
    else:
        write(destination, 'Makefile', 'unrelated')
    with pytest.raises(ValueError):
        PROFILE.prepare_profile(profile)
    assert not calls
    assert destination.exists()


@pytest.mark.parametrize('value', ['/outside', '../outside', 'vendor/../outside', 'vendor\\bad', 'vendor\nother'])
def test_invalid_dts_vendor_rejected_before_any_source_fetch(project, value):
    root, calls = project
    env = board(root)
    env['KERNEL_DTS_SUBDIR'] = value
    with pytest.raises(ValueError, match='KERNEL_DTS_SUBDIR'):
        PROFILE.resolve_profile(root, env)
    assert not calls


def test_unknown_family_cannot_implicitly_inherit_amlogic_vendor(project):
    root, _ = project
    env = board(root)
    del env['KERNEL_DTS_SUBDIR']
    with pytest.raises(ValueError, match='explicitly configured'):
        PROFILE.resolve_profile(root, env)


def test_arm_root_dts_and_zimage_are_selected_without_overwriting_package_arch(project):
    root, _ = project
    env = board(root, arch='arm', vendor='', patch='ARM')
    _, kernel = prepare(root, env)
    result = shell(root, env, 'build_kernel; printf "PACKAGE_ARCH=%s\\n" "$ARCH"')
    assert result.returncode == 0, result.stdout + result.stderr
    assert (kernel / 'arch/arm/boot/zImage').read_text() == 'ARM\n'
    assert (kernel / 'arch/arm/boot/dts/speaker-a.dtb').is_file()
    assert (kernel / 'built-arch').read_text() == 'arm\n'
    assert 'PACKAGE_ARCH=armhf' in result.stdout
    assert not (kernel / 'arch/arm64/boot/Image').exists()


def test_real_package_consumer_uses_profile_not_shared_output_or_legacy_cache(project, tmp_path):
    root, _ = project
    env = board(root)
    _, kernel = prepare(root, env)
    result = shell(root, env, 'build_kernel')
    assert result.returncode == 0, result.stderr
    Path(env['KERNEL_IMAGE']).write_text('wrong shared Image\n')
    Path(env['DTB_IMAGE']).write_text('wrong shared DTB\n')
    write(root, 'cache/kernel/Makefile', 'kernelrelease:\n\t@echo FOREIGN\n')
    commands = tmp_path / 'commands'
    stub = write(commands, 'depmod', '#!/bin/sh\nexit 0\n')
    stub.chmod(0o755)
    env['PATH'] = str(commands) + os.pathsep + os.environ['PATH']
    result = shell(root, env, 'source "$PROJECT_ROOT/lib/packages.sh"; build_kernel_package')
    assert result.returncode == 0, result.stdout + result.stderr
    package = next((root / 'output/packages').glob('*.deb'))
    data = subprocess.check_output(['dpkg-deb', '--fsys-tarfile', str(package)])
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        image = archive.extractfile('./usr/lib/atrios-kernel/7.3.1-profile-test/Image').read()
        dtb = archive.extractfile('./usr/lib/atrios-kernel/7.3.1-profile-test/speaker-a.dtb').read()
        module = archive.extractfile('./usr/lib/modules/7.3.1-profile-test/kernel/selected.ko').read()
    assert image == module == b'A\n'
    assert dtb == (kernel / 'arch/arm64/boot/dts/rockchip/speaker-a.dtb').read_bytes()


def test_profile_resolution_does_not_turn_derived_paths_into_next_board_overrides(project):
    root, _ = project
    a = board(root)
    b = board(root, 'speaker-b', vendor='allwinner', pin='b', patch='B')
    script = 'resolve_kernel_build_profile; '
    for key, value in b.items():
        if key != 'PROJECT_ROOT':
            import shlex
            script += key + '=' + shlex.quote(value) + '; '
    script += 'resolve_kernel_build_profile; printf "%s\\n" "$KERNEL_BUILD_CONFIG_PATH" "$KERNEL_BUILD_DTS_PATH"'
    result = shell(root, a, script)
    assert result.returncode == 0, result.stderr
    assert str(root / 'board/speaker-b/defconfig') in result.stdout
    assert str(root / 'board/speaker-b/dts/speaker-b.dts') in result.stdout
    assert str(root / 'board/speaker-a/defconfig') not in result.stdout


def test_verifier_resolves_each_selected_board_without_local_home_or_shared_cache(project):
    import shlex
    root, calls = project
    script = root / 'scripts/verify_all.sh'
    script.parent.mkdir()
    shutil.copyfile(ROOT / 'scripts/verify_all.sh', script)
    for env in [board(root), board(root, 'speaker-b', vendor='allwinner', pin='b', patch='B')]:
        profile = PROFILE.resolve_profile(root, env)
        values = {**env, 'UPGRADE_DIR': 'upgrade/' + env['BOARD'], 'ROOTFS_OVERLAY': 'overlay/' + env['BOARD'], 'SOC': 'fixture'}
        path = root / 'config/boards' / (env['BOARD'] + '.conf')
        write(root, path.relative_to(root).as_posix(), ''.join(key + '=' + shlex.quote(value) + '\n'
              for key, value in values.items() if key != 'PROJECT_ROOT'))
        result = subprocess.run(['bash', str(script), '--board', env['BOARD'], '--profile-only'],
                                cwd=root, env=os.environ, text=True, capture_output=True)
        assert result.returncode == 0, result.stderr
        assert 'KERNEL_BUILD_DIR=' + profile['build_dir'] in result.stdout
        assert 'DTS_PATH=' + profile['dts'] in result.stdout
        assert 'DTS_SUBDIR=' + env['KERNEL_DTS_SUBDIR'] in result.stdout
        assert not Path(profile['build_dir']).exists()
    assert not calls


def test_failed_make_cannot_report_old_local_artifacts_as_success_when_called_in_condition(project, tmp_path):
    root, _ = project
    local = source_tree(tmp_path / 'already-built')
    write(local, 'Makefile', 'olddefconfig:\n\t@false\nImage dtbs modules:\n\t@true\n')
    write(local, 'arch/arm64/boot/Image', 'obsolete Image\n')
    write(local, 'arch/arm64/boot/dts/rockchip/speaker-a.dtb', 'obsolete DTB\n')
    env = board(root)
    env['KERNEL_SOURCE_DIR'] = str(local)
    prepare(root, env)
    result = shell(root, env, 'if build_kernel; then echo WRONG_SUCCESS; fi')
    assert result.returncode != 0
    assert 'Kernel build failed' in result.stderr
    assert 'WRONG_SUCCESS' not in result.stdout
    assert not Path(env['KERNEL_IMAGE']).exists()
    assert (local / 'arch/arm64/boot/Image').read_text() == 'obsolete Image\n'


def test_explicit_dts_path_must_match_the_selected_logical_board_name(project):
    root, calls = project
    env = board(root)
    mismatch = write(root, 'custom/different-name.dts', '/dts-v1/; / {};\n')
    env['KERNEL_DTS_PATH'] = str(mismatch)
    with pytest.raises(ValueError, match='basename must match DTS_NAME'):
        PROFILE.resolve_profile(root, env)
    assert not calls


def test_kbuild_is_part_of_staged_driver_sources_and_profile_identity(project):
    root, _ = project
    env = board(root)
    before = PROFILE.resolve_profile(root, env)
    write(root, 'drivers/atrios/Kbuild', 'obj-y += test.o\n')
    after, kernel = prepare(root, env)
    assert before['fingerprint'] != after['fingerprint']
    result = shell(root, env, 'build_kernel')
    assert result.returncode == 0, result.stderr
    assert (kernel / 'drivers/atrios/Kbuild').read_text() == 'obj-y += test.o\n'


@pytest.mark.parametrize('kind', ['file', 'directory'])
def test_local_source_escaping_symlink_is_rejected_without_copy_or_mutation(project, tmp_path, kind):
    root, calls = project
    source = source_tree(tmp_path / 'source')
    outside = tmp_path / 'external'
    private = write(outside, 'user.c', 'private preserved\n')
    (source / 'foreign').symlink_to(private if kind == 'file' else outside, target_is_directory=kind == 'directory')
    env = board(root)
    env['KERNEL_SOURCE_DIR'] = str(source)
    with pytest.raises(ValueError, match='escapes its snapshot'):
        PROFILE.resolve_profile(root, env)
    assert private.read_text() == 'private preserved\n'
    assert not (root / 'cache/kernel-profiles').exists()
    assert not calls
