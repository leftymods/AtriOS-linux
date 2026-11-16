"""Local release signing/validation tests. No build, network or device writes."""
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import struct
import subprocess
import sys
import tarfile

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('create_ota_bundle', ROOT / 'tools/create_ota_bundle.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


@pytest.fixture(scope='module')
def rsa_keys(tmp_path_factory):
    directory = tmp_path_factory.mktemp('release-signing')
    private = directory / 'private.pem'
    public = directory / 'public.pem'
    subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048',
                    '-out', str(private)], check=True, capture_output=True)
    subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout', '-out', str(public)],
                   check=True, capture_output=True)
    return private, public


@pytest.fixture
def inputs(tmp_path, rsa_keys):
    kernel = tmp_path / 'Image'
    data = bytearray(128)
    data[56:60] = b'ARM\x64'
    struct.pack_into('<Q', data, 16, len(data))
    kernel.write_bytes(data)
    dtb = tmp_path / 'meson-sm1-yandex-station-max.dtb'
    # Version-17 FDT containing a single empty root node and END marker.
    tree = struct.pack('>I', 1) + bytes(4) + struct.pack('>II', 2, 9)
    dtb.write_bytes(struct.pack('>10I', 0xd00dfeed, 56 + len(tree), 56, 56 + len(tree),
                               40, 17, 16, 0, 0, len(tree)) + bytes(16) + tree)
    rootfs = tmp_path / 'runtime'
    (rootfs / 'etc/atrios').mkdir(parents=True)
    (rootfs / 'etc/atrios/settings.json').write_text('{"enabled":true}\n')
    (rootfs / 'usr/bin').mkdir(parents=True)
    (rootfs / 'usr/bin/example').write_text('#!/bin/sh\nexit 0\n')
    (rootfs / 'usr/bin/example').chmod(0o755)
    modules = tmp_path / 'modules'
    (modules / 'kernel').mkdir(parents=True)
    (modules / 'kernel/example.ko').write_bytes(b'\x7fELFexample-module')
    (modules / 'modules.dep').write_text('kernel/example.ko:\n')
    (modules / 'modules.order').write_text('kernel/example.ko\n')
    (modules / 'modules.alias').write_text('')
    (modules / 'modules.builtin').write_text('')
    return dict(board='station-max', version='1.2.3', kernel_release='7.3.1-AtriOS-aml',
                kernel=kernel, dtb=dtb, rootfs=rootfs, modules=modules,
                signing_key=rsa_keys[0], output_dir=tmp_path / 'release')


def unpack_bundle(bundle, target):
    with tarfile.open(bundle, 'r:gz') as archive:
        for item in archive:
            assert item.isfile()
            path = target / item.name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(archive.extractfile(item).read())
    return json.loads((target / 'manifest.json').read_text())


def verify_manifest(target, public):
    return subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(public),
        '-signature', str(target / 'manifest.sig'), str(target / 'manifest.json')],
        capture_output=True).returncode


def test_signed_bundle_is_deterministic_and_has_exact_checksums(inputs, rsa_keys, tmp_path):
    bundle = builder.build_bundle(**inputs)
    first = bundle.read_bytes()
    builder.build_bundle(**inputs)
    assert bundle.read_bytes() == first
    assert bundle.name == 'AtriOS-station-max-ota.tar.gz'
    assert bundle.with_name(bundle.name + '.sha256').read_text() == hashlib.sha256(first).hexdigest() + '  ' + bundle.name + '\n'
    destination = tmp_path / 'unpacked'
    manifest = unpack_bundle(bundle, destination)
    assert verify_manifest(destination, rsa_keys[1]) == 0
    assert manifest['schema'] == 1 and manifest['device_id'] == 'station-max'
    assert manifest['version'] == '1.2.3' and manifest['kernel_release'] == '7.3.1-AtriOS-aml'
    assert set(manifest['files']) == {'kernel', 'dtb', 'rootfs'}
    for entry in manifest['files'].values():
        assert builder.digest(destination / entry['path']) == entry['sha256']
    with tarfile.open(destination / manifest['files']['rootfs']['path'], 'r:gz') as archive:
        names = archive.getnames()
        assert 'usr/lib/modules/7.3.1-AtriOS-aml/kernel/example.ko' in names
        assert archive.getmember('usr/bin/example').mode == 0o755
        assert all(item.isfile() and item.uid == item.gid == item.mtime == 0 for item in archive)
        assert not any('private.pem' in name for name in names)
    assert not list(inputs['output_dir'].glob('.ota-build-*'))


def test_runtime_accepts_generated_bundle_and_rejects_tampering(inputs, rsa_keys, tmp_path, monkeypatch):
    # Exercise the real runtime verifier, not a second implementation of its schema.
    library = ROOT / 'board/yandex/station2/rootfs_overlay/usr/lib/atrios'
    monkeypatch.syspath_prepend(str(library))
    runtime = runpy.run_path(str(library / 'ota'))
    verify = runtime['verify_bundle']
    monkeypatch.setitem(verify.__globals__, 'device_id', lambda: 'station-max')
    monkeypatch.setitem(verify.__globals__, 'set_error', lambda *a, **kw: None)
    bundle = builder.build_bundle(**inputs)
    destination = tmp_path / 'runtime-unpacked'; destination.mkdir()
    runtime['extract_safe'](bundle, destination)
    config = {'public_key': str(rsa_keys[1])}
    manifest, _ = verify(destination, config)
    assert manifest['device_id'] == 'station-max'
    payload = destination / manifest['files']['kernel']['path']
    payload.write_bytes(b'tampered')
    with pytest.raises(ValueError, match='SHA-256 mismatch'):
        verify(destination, config)
    (destination / 'manifest.json').write_text('{}')
    with pytest.raises(ValueError, match='signature'):
        verify(destination, config)


def test_wrong_verification_key_and_wrong_device_are_rejected(inputs, rsa_keys, tmp_path, monkeypatch):
    bundle = builder.build_bundle(**inputs)
    destination = tmp_path / 'unpacked'
    unpack_bundle(bundle, destination)
    wrong = tmp_path / 'wrong-private.pem'; public = tmp_path / 'wrong-public.pem'
    subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048',
                    '-out', str(wrong)], check=True, capture_output=True)
    subprocess.run(['openssl', 'pkey', '-in', str(wrong), '-pubout', '-out', str(public)], check=True, capture_output=True)
    assert verify_manifest(destination, public) != 0
    library = ROOT / 'board/yandex/station2/rootfs_overlay/usr/lib/atrios'
    monkeypatch.syspath_prepend(str(library))
    verify = runpy.run_path(str(library / 'ota'))['verify_bundle']
    monkeypatch.setitem(verify.__globals__, 'device_id', lambda: 'yndxsttion53')
    with pytest.raises(ValueError, match='wrong target device'):
        verify(destination, {'public_key': str(rsa_keys[1])})


@pytest.mark.parametrize('field,value', [('board', '../station-max'), ('version', '1.2-beta'),
    ('version', 'v1.2.3.4.5'), ('kernel_release', '../../modules')])
def test_rejects_wrong_identifiers(inputs, field, value):
    inputs[field] = value
    with pytest.raises(ValueError): builder.build_bundle(**inputs)
    assert not inputs['output_dir'].exists()


@pytest.mark.parametrize('kind', ['kernel', 'dtb', 'incomplete-modules', 'missing-module'])
def test_invalid_artifacts_preserve_previous_output(inputs, kind):
    output = builder.build_bundle(**inputs)
    before = output.read_bytes()
    if kind == 'kernel': inputs['kernel'].write_bytes(b'not an ARM64 Image')
    if kind == 'dtb': inputs['dtb'].write_bytes(bytes(40))
    if kind == 'incomplete-modules': (inputs['modules'] / 'modules.dep').unlink()
    if kind == 'missing-module': (inputs['modules'] / 'kernel/example.ko').unlink()
    with pytest.raises(ValueError): builder.build_bundle(**inputs)
    assert output.read_bytes() == before
    assert not list(inputs['output_dir'].glob('.ota-build-*'))


@pytest.mark.parametrize('kind', ['link', 'directory-link', 'fifo', 'forbidden', 'private-key'])
def test_unsafe_rootfs_inputs_are_rejected(inputs, kind):
    rootfs = inputs['rootfs']
    if kind == 'link': (rootfs / 'link').symlink_to('/etc/passwd')
    if kind == 'directory-link': (rootfs / 'linked-directory').symlink_to('/etc')
    if kind == 'fifo': os.mkfifo(rootfs / 'fifo')
    if kind == 'forbidden': (rootfs / 'boot').mkdir()
    if kind == 'private-key': shutil.copy(inputs['signing_key'], rootfs / 'accidental-key.pem')
    with pytest.raises(ValueError): builder.build_bundle(**inputs)
    assert not list(inputs['output_dir'].glob('*.tar.gz'))


def test_known_module_build_links_are_excluded_explicitly(inputs, tmp_path):
    (inputs['modules'] / 'build').symlink_to(tmp_path / 'external-kernel-tree')
    (inputs['modules'] / 'source').symlink_to(tmp_path / 'external-kernel-tree')
    bundle = builder.build_bundle(**inputs)
    destination = tmp_path / 'unpacked'
    manifest = unpack_bundle(bundle, destination)
    with tarfile.open(destination / manifest['files']['rootfs']['path'], 'r:gz') as archive:
        assert not any(name.endswith(('/build', '/source')) for name in archive.getnames())


@pytest.mark.parametrize('constant,value', [('MAX_EXPANDED', 512), ('MAX_MEMBERS', 1), ('MAX_DOWNLOAD', 100)])
def test_archive_limits_fail_without_publishing(inputs, monkeypatch, constant, value):
    monkeypatch.setattr(builder, constant, value)
    with pytest.raises(ValueError): builder.build_bundle(**inputs)
    assert not list(inputs['output_dir'].glob('*.tar.gz'))


def test_signing_key_must_be_explicit_rsa_and_never_a_payload(inputs, tmp_path):
    ec_key = tmp_path / 'ec.pem'
    subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256',
                    '-out', str(ec_key)], check=True, capture_output=True)
    inputs['signing_key'] = ec_key
    with pytest.raises(ValueError, match='RSA'): builder.build_bundle(**inputs)
    inputs['signing_key'] = inputs['kernel']
    with pytest.raises(ValueError, match='RSA'): builder.build_bundle(**inputs)


def test_structurally_corrupt_dtb_is_rejected(inputs):
    data = bytearray(inputs['dtb'].read_bytes())
    struct.pack_into('>I', data, 56, 0xdeadbeef)
    inputs['dtb'].write_bytes(data)
    with pytest.raises(ValueError, match='structure token'):
        builder.build_bundle(**inputs)


def test_output_must_be_outside_inputs(inputs):
    inputs['output_dir'] = inputs['rootfs'] / 'release'
    with pytest.raises(ValueError, match='outside'):
        builder.build_bundle(**inputs)


def test_determinism_is_independent_of_process_umask(inputs):
    bundle = builder.build_bundle(**inputs)
    before = bundle.read_bytes()
    previous = os.umask(0o077)
    try:
        builder.build_bundle(**inputs)
    finally:
        os.umask(previous)
    assert bundle.read_bytes() == before


def test_kernel_memory_footprint_may_include_bss(inputs):
    data = bytearray(inputs['kernel'].read_bytes())
    struct.pack_into('<Q', data, 16, 4096)
    inputs['kernel'].write_bytes(data)
    builder.validate_kernel(inputs['kernel'])


def test_workflow_is_separate_and_all_embedded_shell_is_valid():
    import yaml
    workflow = yaml.safe_load((ROOT / '.github/workflows/release-mainline.yml').read_text())
    assert workflow['permissions'] == {'contents': 'read'}
    assert set(workflow['jobs']['build']['strategy']['matrix']['board']) == {'station-max', 'yndxsttion53'}
    assert workflow['jobs']['publish']['permissions'] == {'contents': 'write'}
    for job in workflow['jobs'].values():
        for step in job['steps']:
            if 'run' in step:
                result = subprocess.run(['bash', '-n'], input=step['run'], text=True, capture_output=True)
                assert result.returncode == 0, step['name'] + ': ' + result.stderr


def test_release_stages_normalized_checkout_commands_and_preserves_config_modes(inputs, tmp_path):
    import yaml
    staging = tmp_path / 'output/rootfs_debian_staging'
    commands = ['usr/bin/atrios', 'usr/lib/atrios/ota', 'etc/init.d/S60atrios']
    config = 'etc/atrios/updates.json'
    for name in commands + [config]:
        path = staging / name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('fixture'); path.chmod(0o644)
    # Simulate Git's index-0644 checkout and the real Debian staging normalization.
    result = subprocess.run(['bash', '-euc',
        'sudo() { "$@"; }; source "$1"; prepare_rootfs_command_modes "$2"',
        'test', str(ROOT / 'lib/rootfs-debian.sh'), str(staging)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    workflow = yaml.safe_load((ROOT / '.github/workflows/release-mainline.yml').read_text())
    step = next(s for s in workflow['jobs']['build']['steps'] if s.get('name') == 'Create signed OTA bundle')
    script = step['run'].split("python3 - <<'PY'\n", 1)[1].split('\nPY', 1)[0]
    overlay = tmp_path / 'selected-overlay'
    copied = subprocess.run([sys.executable, '-'], input=script, cwd=tmp_path,
        env=dict(os.environ, OTA_OVERLAY=str(overlay)), capture_output=True, text=True)
    assert copied.returncode == 0, copied.stderr
    inputs['rootfs'] = overlay
    bundle = builder.build_bundle(**inputs)
    unpacked = tmp_path / 'mode-bundle'
    manifest = unpack_bundle(bundle, unpacked)
    with tarfile.open(unpacked / manifest['files']['rootfs']['path'], 'r:gz') as archive:
        for name in commands:
            assert archive.getmember(name).mode & 0o111 == 0o111
        assert archive.getmember(config).mode & 0o111 == 0
