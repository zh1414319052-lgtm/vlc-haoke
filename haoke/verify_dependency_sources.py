# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
"""Verify retained dependency sources without networking, extraction or execution.

The manifest describes a Windows contrib source superset, not a claim that the
unchanged official playback modules were rebuilt locally. Normal tarballs must
match the SHA512SUMS in their pinned VLC contrib recipe. Fixed-Git archives also
retain their exact recipe commit and .githash sidecar.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import stat
import sys
import tarfile


def require(condition, message):
    if not condition:
        raise ValueError(message)


def regular_path(path, root, directory=False):
    """Refuse links/reparse points, including Windows junction parent paths."""
    require(path.is_absolute(), 'Expected an absolute source path')
    require(path.resolve(strict=True).is_relative_to(root.resolve(strict=True)),
            'Source path escapes the selected source root')
    for component in (path, *path.parents):
        attributes = component.lstat()
        require(not stat.S_ISLNK(attributes.st_mode)
                and not (getattr(attributes, 'st_file_attributes', 0)
                         & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0)),
                'Source paths must not contain symbolic links or reparse points')
    attributes = path.stat()
    require(stat.S_ISDIR(attributes.st_mode) if directory else stat.S_ISREG(attributes.st_mode),
            'Expected a regular source directory' if directory else 'Expected a regular source file')
    return path


def simple_name(value):
    require(isinstance(value, str) and value not in ('', '.', '..')
            and '/' not in value and '\\' not in value and ':' not in value,
            'Invalid source filename')
    return value


def no_duplicate_keys(pairs):
    output = {}
    for key, value in pairs:
        require(key not in output, 'Duplicate manifest JSON key')
        output[key] = value
    return output


def hashes(path):
    sha256 = hashlib.sha256()
    sha512 = hashlib.sha512()
    with path.open('rb') as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            sha256.update(chunk)
            sha512.update(chunk)
    return sha256.hexdigest(), sha512.hexdigest()


def verify(root):
    root = Path(root).absolute()
    regular_path(root, root, directory=True)
    manifest_path = regular_path(root / '依赖/source-manifest.json', root)
    require(manifest_path.stat().st_size <= 1024 * 1024, 'Source manifest is oversized')
    manifest = json.loads(manifest_path.read_text(encoding='utf8'), object_pairs_hook=no_duplicate_keys)
    require(manifest.get('schemaVersion') == 1, 'Unknown source manifest schema')
    require(manifest.get('vlcVersion') == '3.0.24', 'Unexpected VLC source version')
    require(manifest.get('pendingGitSources') == [], 'Git dependency sources are pending')
    items = manifest.get('items')
    require(isinstance(items, list) and len(items) == manifest.get('expectedArchiveCount') == 89,
            'Dependency source inventory is incomplete')
    archive_root = regular_path(root / '依赖/源码包', root, directory=True)
    seen = set()
    packages = set()
    total_bytes = 0
    git_count = 0
    for item in items:
        name = simple_name(item.get('filename'))
        package = item.get('package')
        require(isinstance(package, str) and re.fullmatch(r'[A-Za-z0-9_-]+', package),
                'Invalid contrib package name')
        require(name not in seen and package not in packages, 'Duplicate dependency source entry')
        seen.add(name)
        packages.add(package)
        require(item.get('status') == 'verified', 'An unverified dependency source remains: ' + package)
        require(isinstance(item.get('bytes'), int) and 0 < item['bytes'] <= 1024 * 1024 * 1024,
                'Invalid dependency source byte size: ' + package)
        require(isinstance(item.get('sha256'), str) and re.fullmatch('[0-9a-f]{64}', item['sha256'])
                and isinstance(item.get('sha512'), str) and re.fullmatch('[0-9a-f]{128}', item['sha512']),
                'Invalid dependency source hash: ' + package)
        source = regular_path(archive_root / name, root)
        require(source.stat().st_size == item['bytes'], 'Dependency source size mismatch: ' + package)
        actual256, actual512 = hashes(source)
        require(actual256 == item['sha256'] and actual512 == item['sha512'],
                'Dependency source checksum mismatch: ' + package)
        if package in ('x264', 'fluidlite'):
            require(item.get('verification') == 'exact-git-object-and-archive-pax-commit',
                    'Missing Git source provenance: ' + package)
            commit = item.get('gitCommit')
            require(isinstance(commit, str) and re.fullmatch('[0-9a-f]{40}', commit),
                    'Invalid dependency Git commit: ' + package)
            recipe_relative = 'vlc-3.0.24/contrib/src/' + package + '/rules.mak'
            require(item.get('checksumSource') == recipe_relative, 'Unexpected Git recipe evidence path')
            recipe = regular_path(root / recipe_relative, root).read_text(encoding='utf8')
            variable = 'X264_HASH' if package == 'x264' else 'FLUID_HASH'
            require(re.search(r'^' + variable + r'\s*:=\s*' + commit + r'\s*$', recipe, re.M),
                    'Git archive does not match the pinned recipe: ' + package)
            sidecar_name = simple_name(item.get('githashSidecar'))
            require(sidecar_name == name.removesuffix('.tar.xz') + '.githash',
                    'Unexpected Git sidecar name: ' + package)
            sidecar = regular_path(archive_root / sidecar_name, root)
            require(sidecar.stat().st_size <= 512, 'Git sidecar is oversized')
            require(sidecar.read_text(encoding='utf8') == commit + ' ./' + name + '\n',
                    'Git sidecar identity mismatch: ' + package)
            # Only inspect the archive's metadata. No archive members are
            # extracted, imported or executed by this verifier.
            with tarfile.open(source) as archive:
                require(archive.pax_headers.get('comment') == commit,
                        'Git archive PAX commit mismatch: ' + package)
            git_count += 1
        else:
            recipe_relative = 'vlc-3.0.24/contrib/src/' + package + '/SHA512SUMS'
            require(item.get('verification') == 'contrib-sha512sum'
                    and item.get('checksumSource') == recipe_relative,
                    'Unexpected checksum recipe evidence path: ' + package)
            evidence = regular_path(root / recipe_relative, root).read_text(encoding='utf8')
            expected = [line.split() for line in evidence.splitlines() if line.strip()]
            require(any(len(fields) == 2 and fields[0] == actual512 and fields[1] == name
                        for fields in expected),
                    'Source does not match the pinned contrib SHA512SUMS: ' + package)
        total_bytes += item['bytes']
    require(git_count == 2, 'Fixed-Git source inventory is incomplete')
    return {'status': 'PASS', 'archives': len(items), 'bytes': total_bytes,
            'recipeSha512Archives': len(items) - git_count, 'fixedGitArchives': git_count}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).absolute().parents[1])
    args = parser.parse_args()
    try:
        result = verify(args.source_root)
    except (ValueError, OSError, tarfile.TarError, json.JSONDecodeError) as error:
        print(json.dumps({'status': 'FAIL', 'reason': str(error)}, ensure_ascii=False))
        return 1
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
