# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the verified public update identity. No private material is accepted."""
import argparse
import base64
import json
import re
from pathlib import Path
from version import VERSION, VERSION_CODE, APPLICATION_ID, SOFTWARE_ID


def generate(identity):
    required = {'softwareId', 'applicationId', 'platform', 'channel', 'identity',
                'versionName', 'versionCode', 'publicKeys'}
    if set(identity) != required:
        raise ValueError('Unexpected or missing public identity fields')
    if (identity['softwareId'] != SOFTWARE_ID or identity['applicationId'] != APPLICATION_ID
            or identity['platform'] != 'Windows' or identity['channel'] != 'stable'
            or identity['versionName'] != VERSION or identity['versionCode'] != VERSION_CODE):
        raise ValueError('Release identity/version mismatch')
    signing = identity['identity']
    if (set(signing) != {'kind', 'publisher', 'certificateSha256'} or signing['kind'] != 'authenticode'
            or not isinstance(signing['publisher'], str) or not signing['publisher'].strip()
            or len(signing['publisher']) > 500
            or not re.fullmatch('[a-f0-9]{64}', signing['certificateSha256'])):
        raise ValueError('Invalid Authenticode identity')
    pins = identity['publicKeys']
    if not isinstance(pins, dict) or not pins or len(pins) > 8:
        raise ValueError('Missing approved notification trust pin')
    for key, value in pins.items():
        if not re.fullmatch('[A-Za-z0-9_-]{1,100}', key) or len(base64.b64decode(value, validate=True)) != 32:
            raise ValueError('Invalid public notification key')
    literal = lambda value: json.dumps(value, ensure_ascii=False)
    lines = ['/* Generated from verified public release_identity.json; no secrets. */',
             '#ifndef HAOKE_RELEASE_IDENTITY_HPP', '#define HAOKE_RELEASE_IDENTITY_HPP',
             '#include <windows.h>', 'namespace HaokeUpdates {',
             'inline Config productionConfig() {', '    Config c;']
    for field in ('softwareId', 'applicationId', 'channel', 'versionName'):
        lines.append(f'    c.{field} = QString::fromUtf8({literal(identity[field])});')
    for field in ('publisher', 'certificateSha256'):
        lines.append(f'    c.{field} = QString::fromUtf8({literal(signing[field])});')
    lines.append(f'    c.versionCode = {VERSION_CODE}LL; c.architecture = "x64";')
    for key, value in pins.items():
        lines.append(f'    c.publicKeys.insert({literal(key)}, QByteArray::fromBase64({literal(value)}));')
    lines += ['    // RtlGetVersion avoids compatibility-manifest version emulation.',
              '    OSVERSIONINFOW os = {}; os.dwOSVersionInfoSize = sizeof(os);',
              '    typedef LONG (WINAPI *VersionFunction)(OSVERSIONINFOW *);',
              '    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");',
              '    VersionFunction getVersion = ntdll ? reinterpret_cast<VersionFunction>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;',
              '    c.osVersion = getVersion && getVersion(&os) == 0 ? QString("%1.%2.%3").arg(os.dwMajorVersion).arg(os.dwMinorVersion).arg(os.dwBuildNumber) : QString("0");',
              '    return c;', '}', '}', '#endif', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('identity', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    content = generate(json.loads(args.identity.read_text(encoding='utf-8-sig')))
    args.output.write_text(content, encoding='utf-8')
