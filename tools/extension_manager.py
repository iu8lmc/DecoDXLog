#!/usr/bin/env python3
"""Manage trusted, read-only DecoDXLog extension examples in a local directory.

This is developer tooling, not a sandbox or an in-app extension manager.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sys
import tempfile

import extension_harness as harness

MAX_ENTRY = 1024 * 1024


def _root(root, create=False):
    root = pathlib.Path(root)
    if root.is_symlink():
        raise ValueError('extension root must not be a symlink')
    if create:
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
    if not root.is_dir():
        raise ValueError('extension root is unavailable')
    return root.resolve(strict=True)


def _identity(identity):
    harness.validate_manifest({'schemaVersion':1, 'apiVersion':1, 'id':identity, 'version':'1.0.0',
                               'entrypoint':'check.py', 'permissions':['qso.read']})
    return identity


def _directory(root, identity, create=False):
    folder = root / _identity(identity)
    if folder.is_symlink():
        raise ValueError('extension directory must not be a symlink')
    if create and not folder.exists():
        folder.mkdir(mode=0o700)
        (folder / '.decodxlog-extension').write_text(identity, encoding='utf-8')
    if not folder.is_dir():
        raise ValueError('extension is not installed')
    if _regular(folder / '.decodxlog-extension', 128).decode('utf-8') != identity:
        raise ValueError('extension directory is not managed by this tool')
    return folder


def _regular(path, limit):
    path = pathlib.Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError('expected a regular file')
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
    fd = os.open(path, flags)
    with os.fdopen(fd, 'rb') as source:
        data = source.read(limit + 1)
    if len(data) > limit:
        raise ValueError('extension file is too large')
    return data


def _atomic_file(path, data):
    fd, name = tempfile.mkstemp(prefix='.pointer-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as out:
            out.write(data); out.flush(); os.fsync(out.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name): os.unlink(name)


def install(root, manifest_path, token='', port=None, timeout=10):
    manifest_path = pathlib.Path(manifest_path)
    _regular(manifest_path, harness.MAX_MANIFEST)
    manifest = harness.load_manifest(manifest_path)
    harness.supported_scope(manifest)
    files = {name: _regular(manifest_path.parent / name, MAX_ENTRY)
             for name in [manifest['entrypoint']] + manifest.get('dependencies', [])}
    if sum(map(len, files.values())) > 4 * MAX_ENTRY:
        raise ValueError('extension files are too large')
    for name, data in files.items():
        try:
            compile(data, name, 'exec', dont_inherit=True)
        except (SyntaxError, ValueError) as exc:
            raise ValueError('invalid extension Python source') from exc
    manifest_bytes = (json.dumps(manifest, sort_keys=True, ensure_ascii=False, indent=2)+'\n').encode()
    base = _root(root, create=True)
    new_folder = not (base / manifest['id']).exists() and not (base / manifest['id']).is_symlink()
    folder = _directory(base, manifest['id'], create=True)
    try:
        def preflight(directory):
            if port is None:
                return
            harness.run_extension(directory/'manifest.json', token, port, timeout)
            if (_regular(directory/'manifest.json', harness.MAX_MANIFEST) != manifest_bytes or
                    any(_regular(directory/name, MAX_ENTRY) != data for name, data in files.items())):
                raise ValueError('extension changed during preflight')

        version = folder / manifest['version']
        if version.is_symlink():
            raise ValueError('extension version must not be a symlink')
        if version.exists():
            if (not version.is_dir() or _regular(version/'manifest.json', harness.MAX_MANIFEST) != manifest_bytes or
                    any(_regular(version/name, MAX_ENTRY) != data for name, data in files.items())):
                raise ValueError('installed version differs; use a new version')
            preflight(version)
        else:
            stage = pathlib.Path(tempfile.mkdtemp(prefix='.install-', dir=folder))
            try:
                (stage/'manifest.json').write_bytes(manifest_bytes)
                for name, data in files.items():
                    (stage/name).write_bytes(data)
                preflight(stage)
                os.rename(stage, version)
            finally:
                if stage.exists(): shutil.rmtree(stage)
        pointer = {'version':manifest['version'], 'sha256':{name:hashlib.sha256(data).hexdigest() for name, data in files.items()}}
        _atomic_file(folder/'current.json', (json.dumps(pointer, sort_keys=True)+'\n').encode())
        return manifest
    except Exception:
        # Only remove a directory created by this call before its first activation.
        if new_folder and not (folder/'current.json').exists():
            shutil.rmtree(folder)
        raise


def _current(root, identity):
    folder = _directory(_root(root), identity)
    pointer = json.loads(_regular(folder/'current.json', 4096))
    if not isinstance(pointer, dict) or set(pointer) != {'version','sha256'}:
        raise ValueError('invalid extension pointer')
    # Validate version independently before using it as a path component.
    harness.validate_manifest({'schemaVersion':1,'apiVersion':1,'id':identity,'version':pointer['version'],
                               'entrypoint':'check.py','permissions':['qso.read']})
    version = folder / pointer['version']
    if version.is_symlink() or not version.is_dir():
        raise ValueError('invalid extension version directory')
    manifest_path = version/'manifest.json'
    _regular(manifest_path, harness.MAX_MANIFEST)
    manifest = harness.load_manifest(manifest_path)
    if manifest['id'] != identity or manifest['version'] != pointer['version']:
        raise ValueError('extension identity or version changed')
    names = [manifest['entrypoint']] + manifest.get('dependencies', [])
    if not isinstance(pointer['sha256'], dict) or set(pointer['sha256']) != set(names):
        raise ValueError('invalid extension file inventory')
    files = {name: _regular(version/name, MAX_ENTRY) for name in names}
    if sum(map(len, files.values())) > 4 * MAX_ENTRY or any(hashlib.sha256(data).hexdigest() != pointer['sha256'][name] for name, data in files.items()):
        raise ValueError('extension file changed')
    return manifest_path, manifest


def list_installed(root):
    root = _root(root)
    entries = []
    for folder in sorted(root.iterdir()):
        if folder.name.startswith('.'):
            continue
        try:
            _, manifest = _current(root, folder.name)
            entries.append(manifest)
        except (OSError, ValueError, TypeError, UnicodeError, RecursionError):
            # One broken installation must not prevent the operator from
            # inspecting or removing other extensions. Do not include file
            # contents or exception messages in the diagnostic listing.
            entries.append({'id': folder.name, 'status': 'invalid'})
    return entries


def run(root, identity, token, port, timeout):
    manifest_path, _ = _current(root, identity)
    return harness.run_extension(manifest_path, token, port, timeout)


def remove(root, identity):
    folder = _directory(_root(root), identity)
    # The ownership marker allows removal even when an installed version is corrupt.
    shutil.rmtree(folder)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=pathlib.Path, help='dedicated local extension directory')
    commands = parser.add_subparsers(dest='command', required=True)
    add = commands.add_parser('install'); add.add_argument('manifest', type=pathlib.Path)
    add.add_argument('--verify-port', type=int, help='run the candidate with the read-only token before activation')
    add.add_argument('--timeout', type=float, default=10)
    commands.add_parser('list')
    delete = commands.add_parser('remove'); delete.add_argument('id')
    execute = commands.add_parser('run'); execute.add_argument('id'); execute.add_argument('--port', type=int, required=True); execute.add_argument('--timeout', type=float, default=10)
    args = parser.parse_args()
    try:
        if args.command == 'install': result = install(args.root, args.manifest, os.environ.get('DECODXLOG_READ_TOKEN',''), args.verify_port, args.timeout)
        elif args.command == 'list': result = list_installed(args.root)
        elif args.command == 'remove': remove(args.root, args.id); result = {'removed':args.id}
        else: result = run(args.root, args.id, os.environ.get('DECODXLOG_READ_TOKEN',''), args.port, args.timeout)
    except (OSError, ValueError, TypeError, RecursionError, json.JSONDecodeError) as exc:
        print(f'Extension operation failed: {type(exc).__name__}', file=sys.stderr); return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True)); return 0


if __name__ == '__main__':
    sys.exit(main())
