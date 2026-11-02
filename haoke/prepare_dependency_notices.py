# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare byte-preserved runtime license/attribution texts from verified sources.

Run before assembling the portable application. This reads source-manifest.json
and revalidates every retained source archive and pinned contrib recipe. Only
regular text members with explicit license/attribution basenames are copied;
archive extraction, network access and executable content are never used.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import re
import stat
import sys
import tarfile
import zipfile

from verify_dependency_sources import no_duplicate_keys, regular_path, require, verify

MAX_NOTICE_BYTES = 1024 * 1024
NOTICE_NAME = re.compile(
    r'^(?:COPYING|LICENSES?|LICENCES?|COPYRIGHT|NOTICES?|AUTHORS?|CONTRIBUTORS?|'
    r'CREDITS?|THANKS|PATENTS?)(?:[-._].*)?$'
    r'|^(?:GPL(?:V?[123])?|LGPL(?:V?[123])?|BSD)(?:[-._].*)?$'
    r'|^MIT(?:[-._](?:LICENSE|LICENCE|COPYING|txt|md|rst))*$'
    r'|^(?:OFL|FTL)(?:\.(?:txt|md|rst))?$', re.IGNORECASE)
EXECUTABLE_SUFFIXES = frozenset((
    '.exe', '.dll', '.com', '.scr', '.msi', '.bat', '.cmd', '.ps1', '.py',
    '.pyc', '.js', '.mjs', '.cjs', '.vbs', '.sh', '.so', '.a', '.o', '.obj',
    '.pdf', '.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg', '.ico', '.ttf',
    '.otf', '.woff', '.woff2', '.zip', '.gz', '.xz', '.bz2', '.7z',
    '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.cs', '.java', '.go', '.rs',
    '.rb', '.php', '.pl', '.html', '.xml', '.json', '.yaml', '.yml',
))
ATTRIBUTION_NAME = re.compile(r'^(?:AUTHORS?|CONTRIBUTORS?|CREDITS?|THANKS)(?:[-._].*)?$', re.I)
REQUIRED = {
    'qt': {'LICENSE.LGPLv21', 'LICENSE.LGPLv3', 'LICENSE.GPLv3', 'LGPL_EXCEPTION.txt'},
    'qtsvg': {'LICENSE.LGPLv21', 'LICENSE.LGPLv3', 'LICENSE.GPLv3', 'LGPL_EXCEPTION.txt'},
    'gcrypt': {'LICENSES', 'COPYING.LIB'},
    'gpg-error': {'COPYING', 'COPYING.LIB'},
    'amf': {'Platform.h.NOTICE.txt'},
    'crystalhd': {'bc_drv_if.h.NOTICE.txt', 'VLC-COPYING.LIB'},
}
INLINE_NOTICES = {
    'amf': 'amf-headers-v1.5.2/AMF/core/Platform.h',
    'crystalhd': 'bc_drv_if.h',
}


def notice_candidate(package, member_name):
    basename = PurePosixPath(member_name).name
    if PurePosixPath(basename).suffix.lower() in EXECUTABLE_SUFFIXES:
        return False
    if basename == 'LGPL_EXCEPTION.txt' or NOTICE_NAME.fullmatch(basename):
        return True
    # IJG places its actual copyright/license terms in README, rather than a
    # separate LICENSE file. Keep only this explicit package/vendor location.
    parts = PurePosixPath(member_name).parts
    return basename in ('README', 'README.ijg') and (
        (package == 'jpeg' and len(parts) <= 2)
        or (package == 'qt' and '/3rdparty/libjpeg/' in '/' + member_name)
    )


def safe_member_path(member_name):
    require(isinstance(member_name, str) and len(member_name) <= 1024
            and '\\' not in member_name and ':' not in member_name
            and not any(ord(character) < 32 for character in member_name),
            'Invalid license member path')
    relative = PurePosixPath(member_name)
    require(not relative.is_absolute() and relative.parts
            and all(part not in ('', '..') for part in relative.parts),
            'License member path escapes its package')
    return relative


def genuine_text(data):
    if not data or len(data) > MAX_NOTICE_BYTES:
        return False
    if data.startswith((b'MZ', b'\x7fELF', b'%PDF-', b'PK\x03\x04', b'!<arch>')):
        return False
    if data.startswith((b'\xff\xfe', b'\xfe\xff')):
        try:
            decoded = data.decode('utf-16')
        except UnicodeError:
            return False
        return not any(ord(character) < 32 and character not in '\t\r\n\f'
                       for character in decoded)
    return not any(byte < 32 and byte not in (9, 10, 12, 13) for byte in data)


def leading_license_comment(data):
    """Return the original complete leading comments, stopping before code."""
    position = 3 if data.startswith(b'\xef\xbb\xbf') else 0
    comments = 0
    while position < len(data):
        while position < len(data) and data[position] in b' \t\r\n\f':
            position += 1
        if data[position:position + 2] == b'//':
            end = data.find(b'\n', position)
            position = len(data) if end < 0 else end + 1
            comments += 1
        elif data[position:position + 2] == b'/*':
            end = data.find(b'*/', position + 2)
            require(end >= 0, 'Unterminated leading source license comment')
            position = end + 2
            comments += 1
        else:
            break
    result = data[:position]
    require(comments and genuine_text(result) and b'Copyright' in result
            and (b'Permission is hereby granted' in result
                 or b'GNU Lesser General Public License' in result),
            'Required source-header license comment was not found')
    return result


def retain_member(package, relative, data, collected, skipped):
    if relative.as_posix() == INLINE_NOTICES.get(package):
        collected.append((relative.with_name(relative.name + '.NOTICE.txt'),
                          leading_license_comment(data)))
    elif genuine_text(data):
        collected.append((relative, data))
    else:
        skipped.append({'member': relative.as_posix(), 'reason': 'non-text'})


def collect_archive(package, source):
    """Read only whitelisted regular members; never materialize archive paths."""
    collected = []
    skipped = []
    if zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as archive:
            for member in archive.infolist():
                if member.is_dir() or not (notice_candidate(package, member.filename)
                                          or member.filename == INLINE_NOTICES.get(package)):
                    continue
                relative = safe_member_path(member.filename)
                mode = (member.external_attr >> 16) & 0xFFFF
                if stat.S_IFMT(mode) not in (0, stat.S_IFREG):
                    skipped.append({'member': member.filename, 'reason': 'non-regular-member-not-followed'})
                    continue
                require(not (member.flag_bits & 1), 'Encrypted license ZIP member: ' + package)
                if member.file_size > MAX_NOTICE_BYTES:
                    skipped.append({'member': member.filename, 'reason': 'oversized'})
                    continue
                data = archive.read(member)
                retain_member(package, relative, data, collected, skipped)
    else:
        # Seekable compression readers correctly accept the trailing padding
        # used by some upstream bzip2/xz tarballs. No member is extracted.
        with tarfile.open(source, mode='r:*') as archive:
            for member in archive:
                if member.isdir() or not (notice_candidate(package, member.name)
                                         or member.name == INLINE_NOTICES.get(package)):
                    continue
                relative = safe_member_path(member.name)
                if not member.isfile():
                    skipped.append({'member': member.name, 'reason': 'non-regular-member-not-followed'})
                    continue
                if member.size > MAX_NOTICE_BYTES:
                    skipped.append({'member': member.name, 'reason': 'oversized'})
                    continue
                incoming = archive.extractfile(member)
                require(incoming is not None, 'License member could not be read: ' + package)
                data = incoming.read(MAX_NOTICE_BYTES + 1)
                retain_member(package, relative, data, collected, skipped)
    return collected, skipped


def build_notices(root):
    source_summary = verify(root)
    manifest = json.loads((root / '依赖/source-manifest.json').read_text(encoding='utf8'),
                          object_pairs_hook=no_duplicate_keys)
    notices = {}
    package_rows = []
    missing = []
    all_skipped = []
    for item in manifest['items']:
        package = item['package']
        source = regular_path(root / '依赖/源码包' / item['filename'], root)
        try:
            collected, skipped = collect_archive(package, source)
        except (EOFError, tarfile.TarError, zipfile.BadZipFile) as error:
            raise ValueError('Could not read runtime license archive: ' + package
                             + ' (' + type(error).__name__ + ')') from error
        if package == 'crystalhd':
            shared = regular_path(root / 'vlc-3.0.24/COPYING.LIB', root)
            require(shared.stat().st_size <= MAX_NOTICE_BYTES, 'Shared LGPL2.1 text is oversized')
            shared_data = shared.read_bytes()
            require(genuine_text(shared_data) and b'Version 2.1, February 1999' in shared_data,
                    'Shared LGPL2.1 license text is invalid')
            collected.append((PurePosixPath('_shared/VLC-COPYING.LIB'), shared_data))
        names = {relative.name for relative, _ in collected}
        require(REQUIRED.get(package, set()).issubset(names),
                'Required runtime license text is missing: ' + package)
        legal = [relative for relative, _ in collected
                 if not ATTRIBUTION_NAME.fullmatch(relative.name)
                 and not relative.name.endswith('.h.NOTICE.txt')
                 and relative.parts[0] != '_shared']
        if not legal:
            missing.append(package)
        row = {'package': package, 'sourceArchive': item['filename'],
               'sourceSha256': item['sha256'], 'notices': []}
        for relative, data in collected:
            output_relative = PurePosixPath(package) / relative
            key = output_relative.as_posix()
            folded = key.casefold()
            if folded in notices:
                require(notices[folded]['data'] == data and notices[folded]['path'] == key,
                        'Conflicting case-insensitive runtime license path: ' + package)
                continue
            notices[folded] = {'path': key, 'data': data}
            entry = {'sourceMember': relative.as_posix(), 'path': key,
                     'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
            if relative.as_posix() == INLINE_NOTICES.get(package, '') + '.NOTICE.txt':
                entry['sourceMember'] = INLINE_NOTICES[package]
                entry['extraction'] = 'complete-original-leading-copyright-and-license-comment-only'
            elif relative.as_posix() == '_shared/VLC-COPYING.LIB':
                entry['sourceMember'] = None
                entry['sourceEvidence'] = 'vlc-3.0.24/COPYING.LIB'
            row['notices'].append(entry)
        package_rows.append(row)
        all_skipped.extend({'package': package, **value} for value in skipped)
    index = {
        'schemaVersion': 1,
        'sourceManifest': 'source-manifest.json',
        'scope': 'Byte-preserved license/attribution text superset from verified Windows contrib sources; not a statement that all bundled source code is linked into the runtime.',
        'packages': len(package_rows),
        'notices': len(notices),
        'bytes': sum(len(value['data']) for value in notices.values()),
        'missingIndependentLicense': missing,
        'sourceEmbeddedFallbacks': sorted(INLINE_NOTICES),
        'pendingLicenseNotice': [package for package in missing if package not in INLINE_NOTICES],
        'skippedNonTextOrOversized': all_skipped,
        'items': package_rows,
    }
    return source_summary, index, notices


def write_new_or_identical(path, data, root):
    require(path.resolve().is_relative_to(root.resolve()), 'Runtime license output escapes source root')
    for parent in (path.parent, *path.parent.parents):
        if parent.exists():
            regular_path(parent, root if parent.is_relative_to(root) else parent, directory=True)
            break
    if path.exists():
        regular_path(path, root)
        require(path.read_bytes() == data, 'Runtime license output differs; inspect before replacing it')
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    regular_path(path.parent, root, directory=True)
    with path.open('xb') as outgoing:
        outgoing.write(data)


def check_existing_output(output, notices, root):
    if not output.exists():
        return
    regular_path(output, root, directory=True)
    expected_files = {item['path'] for item in notices.values()}
    expected_files.update(('DEPENDENCY-NOTICES.json', 'README.txt'))
    expected_directories = set()
    for filename in expected_files:
        expected_directories.update(parent.as_posix() for parent in PurePosixPath(filename).parents)
    for existing in output.rglob('*'):
        relative = existing.relative_to(output).as_posix()
        if existing.is_dir():
            regular_path(existing, root, directory=True)
            require(relative in expected_directories, 'Unexpected runtime license output directory')
        else:
            regular_path(existing, root)
            require(relative in expected_files, 'Unexpected runtime license output file')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).absolute().parents[1])
    parser.add_argument('--check-only', action='store_true', help='Validate and summarize without writing notice files')
    args = parser.parse_args()
    root = args.source_root.absolute()
    try:
        _, index, notices = build_notices(root)
        require(not index['pendingLicenseNotice'],
                'Dependency license notices still require a verified source fallback')
        output = root / '依赖/运行库许可'
        if not args.check_only:
            check_existing_output(output, notices, root)
            for item in notices.values():
                write_new_or_identical(output / Path(item['path']), item['data'], root)
            index_data = (json.dumps(index, ensure_ascii=False, indent=2) + '\n').encode('utf8')
            write_new_or_identical(output / 'DEPENDENCY-NOTICES.json', index_data, root)
            readme = (
                'Third-party license and attribution notices / 第三方许可与归属通知\n\n'
                'The package directories retain the original notice text bytes from verified source archives.\n'
                '各库目录保留已核验源码包中的原始许可与归属文字，未改写其内容。\n'
                'The inventory is a Windows dependency-source superset; not every source component is necessarily linked.\n'
                '该集合覆盖 Windows 依赖源码集合，并不表示其中每个源码组件均被链接。\n'
                'See DEPENDENCY-NOTICES.json for source archives, member paths and checksums.\n'
                '对应完整源码、上游补丁及构建脚本随浩科优化版源码提供。\n'
            ).encode('utf8')
            write_new_or_identical(output / 'README.txt', readme, root)
        print(json.dumps({key: index[key] for key in ('packages', 'notices', 'bytes', 'missingIndependentLicense',
                                                     'sourceEmbeddedFallbacks', 'pendingLicenseNotice')}
                         | {'status': 'PASS', 'output': str(output), 'checkOnly': args.check_only}, ensure_ascii=False))
    except (ValueError, OSError, UnicodeError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(json.dumps({'status': 'FAIL', 'reason': str(error)}, ensure_ascii=False))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
