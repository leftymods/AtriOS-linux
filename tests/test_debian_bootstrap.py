"""Authenticated Debian bootstrap/cache tests without network, root or chroot."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def shell(code, **env):
    return subprocess.run(['bash', '-euo', 'pipefail', '-c',
        'source "$ROOT/lib/general.sh"; source "$ROOT/lib/rootfs-debian.sh"; ' + code],
        env=dict(os.environ, ROOT=str(ROOT), **env), capture_output=True, text=True)


@pytest.mark.parametrize('kind', ['missing', 'empty', 'directory', 'symlink'])
def test_invalid_archive_keyring_is_rejected(kind, tmp_path):
    keyring = tmp_path / 'archive.gpg'
    if kind == 'empty': keyring.touch()
    if kind == 'directory': keyring.mkdir()
    if kind == 'symlink':
        target = tmp_path / 'actual.gpg'; target.write_bytes(b'trusted')
        keyring.symlink_to(target)
    result = shell('debian_archive_keyring', DEBIAN_ARCHIVE_KEYRING=str(keyring))
    assert result.returncode != 0
    assert 'non-empty regular file' in result.stdout


def test_keyring_override_with_spaces_is_resolved_and_fingerprinted(tmp_path):
    keyring = tmp_path / 'reviewed bookworm.gpg'
    keyring.write_bytes(b'reviewed-keyring-v1')
    code = '''keyring=$(debian_archive_keyring)
printf 'KEYRING=%s\\n' "$keyring"
debian_base_profile bookworm arm64 https://mirror.test/debian python3 "$keyring"
'''
    first = shell(code, DEBIAN_ARCHIVE_KEYRING=str(keyring))
    assert first.returncode == 0, first.stderr
    assert 'KEYRING=' + str(keyring) in first.stdout
    assert 'keyring-sha256=' + hashlib.sha256(keyring.read_bytes()).hexdigest() in first.stdout
    keyring.write_bytes(b'reviewed-keyring-v2')
    second = shell(code, DEBIAN_ARCHIVE_KEYRING=str(keyring))
    assert second.returncode == 0, second.stderr
    assert second.stdout != first.stdout
    assert 'keyring-sha256=' + hashlib.sha256(keyring.read_bytes()).hexdigest() in second.stdout


@pytest.fixture
def bootstrap(tmp_path):
    binaries = tmp_path / 'bin'; binaries.mkdir()
    keyring = tmp_path / 'archive keys.gpg'; keyring.write_bytes(b'reviewed trusted keys')
    cache = tmp_path / 'cache/rootfs_debian_base'; cache.mkdir(parents=True)
    (cache / 'previous-authenticated-base').write_text('keep until success')
    (cache / '.atrios-base-profile').write_text('previous-profile\n')
    capture = tmp_path / 'bootstrap-argv.json'
    scripts = {
        'qemu-aarch64-static': '#!/bin/sh\nexit 0\n',
        'sudo': '#!/bin/sh\nexec "$@"\n',
        'debootstrap': '''#!/usr/bin/python3
import json, os, pathlib, sys
pathlib.Path(os.environ['CAPTURE']).write_text(json.dumps(sys.argv[1:]))
work=pathlib.Path(sys.argv[-2])
(work/'usr/bin').mkdir(parents=True)
(work/'debootstrap').mkdir()
raise SystemExit(int(os.environ.get('FIRST_STAGE_RC','0')))
''',
        'cp': '''#!/bin/sh
if [ "$1" = /usr/bin/qemu-aarch64-static ]; then
    : > "$2/qemu-aarch64-static"
else
    exec /bin/cp "$@"
fi
''',
        'chroot': '''#!/bin/sh
[ "${SECOND_STAGE_RC:-0}" = 0 ] || exit "$SECOND_STAGE_RC"
touch "$1/second-stage-finished"
''',
        'mv': '''#!/usr/bin/python3
import os,pathlib,subprocess,sys
source,destination=map(pathlib.Path,sys.argv[-2:])
if os.environ.get('PUBLISH_FAIL')=='1' and destination.name=='rootfs_debian_base' and not source.name.endswith('.previous'):
    raise SystemExit(42)
raise SystemExit(subprocess.run(['/bin/mv',*sys.argv[1:]]).returncode)
''',
    }
    for name, source in scripts.items():
        path = binaries / name; path.write_text(source); path.chmod(0o755)
    def run(**options):
        env = dict(PATH=str(binaries) + os.pathsep + os.environ['PATH'],
            CACHE=str(cache), KEYRING=str(keyring), CAPTURE=str(capture), **options)
        result = shell('bootstrap_debian_base "$CACHE" bookworm arm64 https://mirror.test/debian python3 "$KEYRING" new-profile', **env)
        return result
    return run, cache, keyring, capture


def test_successful_bootstrap_uses_verified_keyring_and_publishes_complete_base(bootstrap):
    run, cache, keyring, capture = bootstrap
    result = run()
    assert result.returncode == 0, result.stdout + result.stderr
    argv = json.loads(capture.read_text())
    assert '--keyring=' + str(keyring) in argv
    assert not any(flag in argv for flag in ['--no-check-gpg', '--no-check-certificate', '--force-check-gpg=false'])
    assert '--foreign' in argv and 'bookworm' in argv
    assert (cache / 'second-stage-finished').exists()
    assert (cache / '.atrios-base-profile').read_text() == 'new-profile\n'
    assert not (cache / 'previous-authenticated-base').exists()
    assert not list(cache.parent.glob('.rootfs-debian-base.*'))


@pytest.mark.parametrize('failure', ['first-stage', 'second-stage', 'publish'])
def test_failed_bootstrap_preserves_previous_authenticated_base(bootstrap, failure):
    run, cache, _, _ = bootstrap
    options = {'first-stage': {'FIRST_STAGE_RC': '37'},
        'second-stage': {'SECOND_STAGE_RC': '23'}, 'publish': {'PUBLISH_FAIL': '1'}}[failure]
    result = run(**options)
    assert result.returncode != 0
    assert (cache / 'previous-authenticated-base').read_text() == 'keep until success'
    assert (cache / '.atrios-base-profile').read_text() == 'previous-profile\n'
    assert not list(cache.parent.glob('.rootfs-debian-base.*'))


def test_bootstrap_refuses_cache_symlink_before_mutating_target(bootstrap, tmp_path):
    run, cache, _, capture = bootstrap
    saved = tmp_path / 'saved'; cache.rename(saved); cache.symlink_to(saved)
    result = run()
    assert result.returncode != 0
    assert (saved / 'previous-authenticated-base').read_text() == 'keep until success'
    assert cache.is_symlink()
    assert not capture.exists()