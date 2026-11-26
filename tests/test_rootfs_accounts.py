"""Real rsync and ncurses checks; sudo/chroot are unprivileged adapters."""
import os
from pathlib import Path
import shutil
import stat
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / 'lib/rootfs-debian.sh'


def write(root, name, data, mode=0o644):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    path.chmod(mode)
    return path


def command(root, name, content):
    return write(root, name, content.encode(), 0o755)


def shell(tmp_path, source, **variables):
    commands = tmp_path / 'commands'
    commands.mkdir(exist_ok=True)
    command(commands, 'sudo', '#!/bin/sh\nexec "$@"\n')
    env = {**os.environ, 'PATH': str(commands) + os.pathsep + os.environ['PATH'],
           'RECIPE': str(RECIPE), **{key: str(value) for key, value in variables.items()}}
    setup = 'exit_with_error() { printf "%s\\n" "$*" >&2; exit 44; }; source "$RECIPE"; '
    return subprocess.run(['bash', '-euo', 'pipefail', '-c', setup + source],
                          env=env, capture_output=True, text=True)


def account_tree(stage):
    files = {
        'passwd': (b'root:x:0:0:root:/root:/bin/bash\n_apt:x:42:65534::/nonexistent:/usr/sbin/nologin\n', 0o644),
        'group': (b'root:x:0:\nnogroup:x:65534:\n', 0o644),
        'shadow': (b'root:!:20000:0:99999:7:::\n_apt:!:20000:0:99999:7:::\n', 0o640),
        'gshadow': (b'root:*::\nnogroup:*::\n', 0o600),
    }
    return [write(stage, 'etc/' + name, data, mode) for name, (data, mode) in files.items()]


def snapshot(paths):
    return {str(path): (path.read_bytes(), stat.S_IMODE(path.stat().st_mode),
                        path.stat().st_uid, path.stat().st_gid) for path in paths}


def require(tool):
    if shutil.which(tool) is None:
        pytest.skip(tool + ' is required for this integration test')


def test_real_overlay_preserves_accounts_modes_and_package_users(tmp_path):
    require('rsync')
    stage, overlay = tmp_path / 'stage', tmp_path / 'custom board overlay'
    accounts = account_tree(stage)
    before = snapshot(accounts)
    for name in ('passwd', 'group', 'shadow', 'gshadow'):
        write(overlay, 'etc/' + name, b'root:stale-overlay\n', 0o777)
    conf = write(overlay, 'etc/custom-speaker.conf', b'BOARD_FEATURE=enabled\n', 0o640)
    command(overlay, 'usr/bin/speaker-command', '#!/bin/sh\nexit 0\n')
    write(overlay, 'usr/share/speaker/etc/passwd', b'application fixture\n')
    result = shell(tmp_path, 'apply_rootfs_overlay "$OVERLAY" "$STAGE"', OVERLAY=overlay, STAGE=stage)
    assert result.returncode == 0, result.stderr
    assert snapshot(accounts) == before
    assert b'_apt:x:42:65534:' in (stage / 'etc/passwd').read_bytes()
    assert (stage / 'etc/custom-speaker.conf').read_bytes() == conf.read_bytes()
    assert stat.S_IMODE((stage / 'etc/custom-speaker.conf').stat().st_mode) == 0o640
    assert os.access(stage / 'usr/bin/speaker-command', os.X_OK)
    assert (stage / 'usr/share/speaker/etc/passwd').read_bytes() == b'application fixture\n'


@pytest.mark.parametrize('kind', ['symlink', 'directory'])
def test_protected_account_entries_cannot_be_replaced_by_aliases_or_directories(tmp_path, kind):
    require('rsync')
    stage, overlay = tmp_path / 'stage', tmp_path / 'overlay'
    accounts = account_tree(stage)
    before = snapshot(accounts)
    for name in ('passwd', 'group', 'shadow', 'gshadow'):
        target = overlay / 'etc' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if kind == 'symlink':
            target.symlink_to(tmp_path / 'nonexistent-' / name)
        else:
            target.mkdir()
            (target / 'injected').write_text('invalid account database')
    result = shell(tmp_path, 'apply_rootfs_overlay "$OVERLAY" "$STAGE"', OVERLAY=overlay, STAGE=stage)
    assert result.returncode == 0, result.stderr
    assert snapshot(accounts) == before


@pytest.mark.parametrize('side', ['stage', 'overlay'])
@pytest.mark.parametrize('kind', ['symlink', 'file'])
def test_overlay_rejects_protected_etc_parent_replacement_before_copy(tmp_path, side, kind):
    require('rsync')
    stage, overlay = tmp_path / 'stage', tmp_path / 'overlay'
    account_tree(stage)
    write(overlay, 'etc/settings', b'new settings')
    external = tmp_path / 'external'
    preserved = write(external, 'etc/passwd', b'outside untouched\n')
    target = (stage if side == 'stage' else overlay) / 'etc'
    shutil.rmtree(target)  # Only this test's own newly created fixture directory.
    if kind == 'symlink':
        target.symlink_to(external / 'etc', target_is_directory=True)
    else:
        target.write_text('not a directory')
    result = shell(tmp_path, 'apply_rootfs_overlay "$OVERLAY" "$STAGE"', OVERLAY=overlay, STAGE=stage)
    assert result.returncode == 44
    assert 'must be a real directory' in result.stderr
    assert preserved.read_bytes() == b'outside untouched\n'
    assert not (external / 'etc/settings').exists()
    assert not (stage / 'etc/settings').exists()


def terminal_tree(tmp_path):
    require('tic')
    require('infocmp')
    stage = tmp_path / 'terminal stage'
    database = stage / 'lib/terminfo'
    database.mkdir(parents=True)
    src = tmp_path / 'terminals.src'
    src.write_text('\n'.join(
        term + '|fixture for ' + term + ',\n  am, cols#80, lines#24, clear=\\E[H\\E[2J,\n'
        for term in ('linux', 'xterm', 'ansi', 'dumb')))
    subprocess.run(['tic', '-x', '-o', str(database), str(src)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    commands = tmp_path / 'commands'
    commands.mkdir(exist_ok=True)
    command(commands, 'chroot', '''#!/bin/sh
stage=$1
shift
[ "$1" = /usr/bin/infocmp ] || exit 91
shift
printf '%s\n' "$*" >> "$CAPTURE"
exec "$REAL_INFOCMP" -A "$stage/lib/terminfo" "$@"
''')
    return stage, database


def test_terminal_validation_reads_all_four_actual_compiled_entries(tmp_path):
    stage, _ = terminal_tree(tmp_path)
    capture = tmp_path / 'calls'
    result = shell(tmp_path, 'validate_rootfs_terminfo "$STAGE"', STAGE=stage,
                   REAL_INFOCMP=shutil.which('infocmp'), CAPTURE=capture)
    assert result.returncode == 0, result.stderr
    assert capture.read_text().splitlines() == ['-x ' + term for term in ('linux', 'xterm', 'ansi', 'dumb')]


@pytest.mark.parametrize('term', ['linux', 'xterm', 'ansi', 'dumb'])
@pytest.mark.parametrize('problem', ['missing', 'corrupt'])
def test_terminal_validation_fails_closed_for_each_required_entry(tmp_path, term, problem):
    stage, database = terminal_tree(tmp_path)
    entry = next(path for path in database.rglob(term) if path.is_file())
    if problem == 'missing':
        entry.unlink()
    else:
        entry.write_bytes(b'invalid compiled terminal data')
    capture = tmp_path / 'calls'
    result = shell(tmp_path, 'validate_rootfs_terminfo "$STAGE"', STAGE=stage,
                   REAL_INFOCMP=shutil.which('infocmp'), CAPTURE=capture)
    assert result.returncode == 44
    assert 'missing or unreadable: ' + term in result.stderr
    assert capture.read_text().splitlines()[-1] == '-x ' + term


def test_recipe_preserves_authentication_and_validates_after_stripping():
    source = RECIPE.read_text()
    executable_lines = [line.strip() for line in source.splitlines()
                        if line.strip() and not line.lstrip().startswith('#')]
    assert not any('root:root' in line or 'chpasswd' in line for line in executable_lines)
    assert 'apply_rootfs_overlay "$PROJECT_ROOT/$ROOTFS_OVERLAY" "$stage_dir"' in source
    assert 'sudo sed -i' in source and 'root:/root:/usr/bin/fish' in source
    assert 'ncurses-base,ncurses-bin' in source
    assert source.index('"$stage_dir/usr/share/man"') < source.index('validate_rootfs_terminfo "$stage_dir"')
    assert source.index('validate_rootfs_terminfo "$stage_dir"') < source.index('sudo rm -f "$stage_dir/usr/bin/qemu-aarch64-static"')
