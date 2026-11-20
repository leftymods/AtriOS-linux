"""Exercise release workflow gates and public runtime staging without builds."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('stage_ota_runtime', ROOT / 'tools/stage_ota_runtime.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


def put(root, name, data=b'data', mode=0o644):
    target = root / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    target.chmod(mode)
    return target


@pytest.fixture
def built(tmp_path):
    root = tmp_path / 'built'
    root.mkdir()
    put(root, 'etc/atrios-board', b'speaker-test\n')
    put(root, 'etc/atrios-version', b'1.2.3\n')
    put(root, 'etc/os-release', b'ID=atrios\nVERSION_ID="1.2.3"\n')
    put(root, 'etc/atrios/ota-public.pem', b'-----BEGIN PUBLIC KEY-----\npublic\n-----END PUBLIC KEY-----\n')
    put(root, 'etc/atrios/updates.json', b'{"repository":"owner/repo","public_key":"/etc/atrios/ota-public.pem"}')
    put(root, 'usr/lib/atrios/ota', b'#!/usr/bin/env python3\nprint("OTA")\n', 0o755)
    put(root, 'usr/lib/atrios/atriai', b'#!/usr/bin/env python3\n', 0o755)
    put(root, 'usr/lib/atrios/atrios-runtime', b'ELF-runtime\n', 0o755)
    put(root, 'usr/lib/atrios/console.sh', b'# selected console helper\n', 0o755)
    put(root, 'etc/atrios-console', b'ttyS2,1500000\n')
    put(root, 'etc/init.d/S01modules', b'#!/bin/sh\nexit 0\n', 0o755)
    put(root, 'etc/init.d/S50board-helper', b'#!/bin/sh\nexit 0\n', 0o755)
    put(root, 'usr/share/fish/functions/fish_command_not_found.fish', b'function fish_command_not_found\nend\n')
    put(root, 'lib/firmware/regulatory.db-upstream', b'database')
    put(root, 'lib/firmware/regulatory.db.p7s-upstream', b'signature')
    return root


def stage(built, tmp_path, **kwargs):
    destination = tmp_path / 'public-runtime'
    names = runtime.stage_runtime(built, destination, 'speaker-test', '1.2.3', **kwargs)
    return destination, names


def test_staged_runtime_includes_branding_regdb_console_and_normalized_fish(built, tmp_path):
    out, names = stage(built, tmp_path)
    for name in ['etc/os-release', 'lib/firmware/regulatory.db-upstream',
                 'lib/firmware/regulatory.db.p7s-upstream',
                 'usr/share/fish/functions/fish_command_not_found.fish',
                 'etc/init.d/S50board-helper', 'etc/atrios-console',
                 'usr/lib/atrios/console.sh']:
        assert name in names
        assert (out / name).read_bytes() == (built / name).read_bytes()
    assert (out / 'usr/lib/atrios/ota').stat().st_mode & 0o777 == 0o755
    # The installer owns this transaction marker and rejects duplicate targets.
    assert not (out / 'etc/atrios-version').exists()


def test_default_payload_excludes_device_secrets_aec_and_legacy_server(built, tmp_path):
    private = ['root/.config/fish/config.fish', 'var/lib/atrios/ai.json',
        'etc/atrios/token', 'etc/atrios/voice-ca.pem', 'etc/shadow',
        'usr/lib/atrios/atrios-api']
    for name in private: put(built, name, b'private sentinel')
    out, names = stage(built, tmp_path)
    for name in private:
        assert name not in names
        assert not (out / name).exists()


@pytest.mark.parametrize('name,value', [('etc/atrios-board', b'other-speaker\n'),
    ('etc/atrios-version', b'1.0\n'), ('etc/os-release', b'VERSION_ID="1.0"\n')])
def test_mismatched_selected_board_or_tag_fails_before_output(built, tmp_path, name, value):
    put(built, name, value)
    with pytest.raises(ValueError): stage(built, tmp_path)
    assert not (tmp_path / 'public-runtime').exists()


def test_os_release_symlink_inside_usrmerged_tree_is_materialized(built, tmp_path):
    old = (built / 'etc/os-release').read_bytes()
    (built / 'etc/os-release').unlink()
    put(built, 'usr/lib/os-release', old)
    (built / 'etc/os-release').symlink_to('../usr/lib/os-release')
    out, _ = stage(built, tmp_path)
    assert not (out / 'etc/os-release').is_symlink()
    assert (out / 'etc/os-release').read_bytes() == old


def test_usrmerged_firmware_pair_stays_canonical_lib_in_archive_input(built, tmp_path):
    (built / 'usr/lib').mkdir(exist_ok=True, parents=True)
    shutil.move(built / 'lib/firmware', built / 'usr/lib/firmware')
    (built / 'lib').rmdir()
    (built / 'lib').symlink_to('usr/lib')
    out, names = stage(built, tmp_path)
    assert 'lib/firmware/regulatory.db-upstream' in names
    assert not any(name.startswith('usr/lib/firmware/') for name in names)
    assert (out / 'lib/firmware/regulatory.db-upstream').read_bytes() == b'database'


def test_partial_signed_regdb_pair_is_not_published(built, tmp_path):
    (built / 'lib/firmware/regulatory.db.p7s-upstream').unlink()
    with pytest.raises(ValueError, match='complete'): stage(built, tmp_path)
    assert not (tmp_path / 'public-runtime').exists()


def test_board_manifest_adds_reviewed_custom_runtime_without_core_changes(built, tmp_path):
    put(built, 'usr/lib/speaker-beta/render', b'#!/bin/sh\n', 0o755)
    manifest = tmp_path / 'runtime.json'
    manifest.write_text(json.dumps({'schema': 1, 'paths': ['usr/lib/speaker-beta']}))
    out, names = stage(built, tmp_path, runtime_manifest=manifest)
    assert 'usr/lib/speaker-beta/render' in names
    assert (out / 'usr/lib/speaker-beta/render').stat().st_mode & 0o777 == 0o755


@pytest.mark.parametrize('name', ['../outside', '/etc/shadow', 'root/private',
    'var/lib/atrios/ai.json', 'etc/passwd', 'etc/ssh/ssh_host_rsa_key',
    'etc/atrios', 'etc/atrios/ai.json',
    'usr/lib/atrios/atrios-api', 'usr/lib/vendor/bot-token', 'usr/lib/vendor/private.key',
    'usr/lib/vendor\\escape', 'usr/lib/vendor\x00bad'])
def test_manifest_cannot_select_private_legacy_or_noncanonical_paths(built, tmp_path, name):
    manifest = tmp_path / 'runtime.json'
    manifest.write_text(json.dumps({'schema': 1, 'paths': [name]}))
    with pytest.raises(ValueError): stage(built, tmp_path, runtime_manifest=manifest)
    assert not (tmp_path / 'public-runtime').exists()


@pytest.mark.parametrize('kind', ['private-key', 'json-secret', 'escape-link', 'special-file'])
def test_runtime_stager_rejects_secret_data_escape_and_special_files(built, tmp_path, kind):
    path = 'usr/lib/vendor/helper'
    if kind == 'private-key': put(built, path, b'-----BEGIN PRIVATE KEY-----\nsecret\n')
    if kind == 'json-secret':
        path += '.json'; put(built, path, b'{"nested":{"token":"secret"}}')
    if kind == 'escape-link':
        target = tmp_path / 'outside'; target.write_text('outside')
        put(built, path).unlink(); (built / path).symlink_to(target)
    if kind == 'special-file':
        (built / path).parent.mkdir(parents=True); os.mkfifo(built / path)
    manifest = tmp_path / 'runtime.json'
    manifest.write_text(json.dumps({'schema': 1, 'paths': [path]}))
    with pytest.raises(ValueError): stage(built, tmp_path, runtime_manifest=manifest)
    assert not (tmp_path / 'public-runtime').exists()


def test_ci_test_discovery_executes_every_new_top_level_firmware_test(tmp_path):
    project = tmp_path / 'project'
    (project / '.github/scripts').mkdir(parents=True)
    shutil.copyfile(ROOT / '.github/scripts/run-firmware-tests.sh', project / '.github/scripts/run-firmware-tests.sh')
    (project / 'tests/nested').mkdir(parents=True)
    for name in ['test_board_profiles.py', 'test_rootfs_startup.py', 'test_new_speaker.py', 'test_telegram_codex_bridge.py']:
        (project / 'tests' / name).touch()
    (project / 'tests/nested/test_hardware_build.py').touch()
    binaries = tmp_path / 'bin'; binaries.mkdir()
    capture = tmp_path / 'args'
    (binaries / 'python3').write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$CAPTURE"\n')
    (binaries / 'python3').chmod(0o755)
    result = subprocess.run(['bash', str(project / '.github/scripts/run-firmware-tests.sh')],
        env=dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ['PATH'], CAPTURE=str(capture)),
        capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    args = capture.read_text().splitlines()
    for name in ['test_board_profiles.py', 'test_rootfs_startup.py', 'test_new_speaker.py']:
        assert 'tests/' + name in args
    assert 'tests/test_telegram_codex_bridge.py' not in args
    assert 'tests/nested/test_hardware_build.py' not in args
    assert 'tests/tier2_boundaries/test_aml_c_regression.py' in args


def test_both_mainline_workflows_use_shared_discovery_and_run_blocks_parse():
    for name in ['mainline-checks.yml', 'release-mainline.yml']:
        workflow = yaml.safe_load((ROOT / '.github/workflows' / name).read_text())
        runs = [step['run'] for job in workflow['jobs'].values() for step in job.get('steps', []) if 'run' in step]
        assert any('bash .github/scripts/run-firmware-tests.sh' in run for run in runs)
        for run in runs:
            result = subprocess.run(['bash', '-n'], input=run, capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
    release = (ROOT / '.github/workflows/release-mainline.yml').read_text()
    assert 'python3 tools/stage_ota_runtime.py' in release
    assert '--runtime-manifest "$OTA_RUNTIME_MANIFEST_PATH"' in release
    assert 'resolve_kernel_build_profile' in release
    assert 'cat "$KERNEL_BUILD_DIR/include/config/kernel.release"' in release
    assert 'cat cache/kernel/' not in release


@pytest.mark.parametrize('kind', ['inside', 'outside', 'absolute', 'missing', 'directory', 'newline'])
def test_board_runtime_manifest_must_resolve_to_a_project_file(tmp_path, kind):
    project = tmp_path / 'project'
    for relative in ['lib', 'config/boards', 'config/families', 'board/test/overlay', 'runtime']:
        (project / relative).mkdir(parents=True, exist_ok=True)
    for name in ['general.sh', 'config.sh', 'release-board.sh']:
        shutil.copyfile(ROOT / 'lib' / name, project / 'lib' / name)
    manifest = project / 'runtime/board.json'
    manifest.write_text('{"schema":1,"paths":[]}')
    setting = 'runtime/board.json'
    if kind == 'outside':
        external = tmp_path / 'external.json'; external.write_text('{}')
        manifest.unlink(); manifest.symlink_to(external)
    if kind == 'absolute': setting = str(manifest)
    if kind == 'missing': setting = 'runtime/missing.json'
    if kind == 'directory': setting = 'runtime'
    if kind == 'newline': setting += '\n'
    import shlex
    (project / 'config/boards/custom.conf').write_text('''BOARD="custom"
BOARD_NAME="Custom speaker"
BOARDFAMILY="test"
SOC="test"
ARCH="arm64"
DTS_NAME="custom-speaker"
ROOTFS_OVERLAY="board/test/overlay"
UPGRADE_DIR="device/test/upgrade"
OTA_RUNTIME_MANIFEST=''' + shlex.quote(setting) + '\n')
    command = 'source "$PROJECT_ROOT/lib/release-board.sh"; release_board_config custom; printf "MANIFEST=%s\\n" "$OTA_RUNTIME_MANIFEST_PATH"'
    result = subprocess.run(['bash', '-euo', 'pipefail', '-c', command],
        env=dict(os.environ, PROJECT_ROOT=str(project)), capture_output=True, text=True)
    assert (result.returncode == 0) == (kind == 'inside'), result.stdout + result.stderr
    if kind == 'inside': assert 'MANIFEST=' + str(manifest) in result.stdout


def test_public_json_alias_cannot_hide_credential_fields(built, tmp_path):
    payload = put(built, 'usr/lib/vendor/settings.data', b'{"token":"private"}')
    (built / 'etc/atrios/updates.json').unlink()
    (built / 'etc/atrios/updates.json').symlink_to('../../usr/lib/vendor/settings.data')
    with pytest.raises(ValueError, match='Credential'): stage(built, tmp_path)
    assert not (tmp_path / 'public-runtime').exists()


def test_missing_upstream_pair_cannot_silently_omit_wireless_update(built, tmp_path):
    for name in ['regulatory.db-upstream', 'regulatory.db.p7s-upstream']:
        (built / 'lib/firmware' / name).unlink()
    with pytest.raises(ValueError, match='complete'): stage(built, tmp_path)
    assert not (tmp_path / 'public-runtime').exists()


@pytest.mark.parametrize('kind', ['RSA', 'EC', 'DSA', 'OPENSSH', 'ENCRYPTED', 'PGP'])
def test_all_pem_private_key_types_are_excluded(built, tmp_path, kind):
    name = 'usr/lib/vendor/helper'
    suffix = ' BLOCK' if kind == 'PGP' else ''
    put(built, name, ('-----BEGIN ' + kind + ' PRIVATE KEY' + suffix + '-----\nsecret\n').encode())
    manifest = tmp_path / 'runtime.json'
    manifest.write_text(json.dumps({'schema': 1, 'paths': [name]}))
    with pytest.raises(ValueError, match='Private'): stage(built, tmp_path, runtime_manifest=manifest)
    assert not (tmp_path / 'public-runtime').exists()


def test_absolute_os_release_symlink_uses_chroot_semantics(built, tmp_path):
    original = (built / 'etc/os-release').read_bytes()
    put(built, 'usr/lib/os-release', original)
    (built / 'etc/os-release').unlink()
    (built / 'etc/os-release').symlink_to('/usr/lib/os-release')
    out, _ = stage(built, tmp_path)
    assert (out / 'etc/os-release').read_bytes() == original
    assert not (out / 'etc/os-release').is_symlink()


def test_absolute_usrmerge_symlink_does_not_read_host_firmware(built, tmp_path):
    shutil.move(built / 'lib/firmware', built / 'usr/lib/firmware')
    (built / 'lib').rmdir()
    (built / 'lib').symlink_to('/usr/lib')
    out, _ = stage(built, tmp_path)
    assert (out / 'lib/firmware/regulatory.db-upstream').read_bytes() == b'database'


@pytest.mark.parametrize('target', ['var/lib/atrios/ai.json', 'root/secret', 'home/user/token', 'etc/shadow'])
def test_public_runtime_alias_cannot_select_private_inside_root(built, tmp_path, target):
    put(built, target, b'{"token":"never publish"}')
    (built / 'usr/lib/atrios/atriai').unlink()
    (built / 'usr/lib/atrios/atriai').symlink_to('/' + target)
    with pytest.raises(ValueError): stage(built, tmp_path)
    assert not (tmp_path / 'public-runtime').exists()


def test_credential_json_in_public_extensionless_runtime_is_rejected(built, tmp_path):
    put(built, 'usr/lib/vendor/config.data', b'{"nested":{"token":"private"}}')
    (built / 'usr/lib/atrios/atriai').unlink()
    (built / 'usr/lib/atrios/atriai').symlink_to('../vendor/config.data')
    with pytest.raises(ValueError, match='Credential'): stage(built, tmp_path)
    assert not (tmp_path / 'public-runtime').exists()


@pytest.mark.parametrize('kind', ['loop', 'relative-root-escape'])
def test_runtime_symlink_loop_and_relative_root_escape_fail_bounded(built, tmp_path, kind):
    (built / 'usr/lib/atrios/atriai').unlink()
    if kind == 'loop':
        (built / 'usr/lib/atrios/atriai').symlink_to('loop')
        (built / 'usr/lib/atrios/loop').symlink_to('atriai')
    else:
        (built / 'usr/lib/atrios/atriai').symlink_to('../../../../outside')
    with pytest.raises(ValueError): stage(built, tmp_path)
    assert not (tmp_path / 'public-runtime').exists()