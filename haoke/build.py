# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
"""Windows + WSL reproducible QA assembly; rebuilds native entry and Qt UI.
Usage: python haoke/build.py --qt-plugin PATH --work-dir PATH --output PATH
Requires Python polib, WSL Ubuntu mingw-w64 and gcc (see README.md).
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
import polib
from localize import catalogs
from version import VERSION, APPLICATION_ID, SOFTWARE_ID

OFFICIAL_SHA256 = 'fcf30850371ad10c9373cc4f0f4501e7dee49e3e9ae9f20c72fb2661a1ca6323'

def sha(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def linux(path):
    path = Path(path).resolve()
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--official-zip', type=Path,
                        default=Path(__file__).resolve().parents[1] / '依赖/VLC官方运行库_3.0.24.zip')
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qt-plugin', type=Path, required=True,
                        help='Rebuilt Qt plugin from haoke/build-qt.sh')
    parser.add_argument('--resume', action='store_true', help='Resume this exact work directory')
    parser.add_argument('--stage', choices=('qa', 'formal'), default='qa')
    parser.add_argument('--signing-metadata', type=Path,
                        help='External public signing identity metadata; never a private key')
    parser.add_argument('--qt-build-metadata', type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    work = args.work_dir.resolve()
    formal = args.stage == 'formal'
    subprocess.run([sys.executable, '-X', 'utf8',
                    str(source / 'haoke/prepare_dependency_notices.py')], check=True)
    if formal:
        if not args.signing_metadata or not args.signing_metadata.is_file():
            raise SystemExit('Formal build requires the verified external signing identity')
        release_path = source / 'haoke/release_identity.json'
        if not release_path.is_file():
            raise SystemExit('Formal update identity has not been registered and verified')
        release = json.loads(release_path.read_text(encoding='utf-8-sig'))
        signing = json.loads(args.signing_metadata.read_text(encoding='utf-8-sig'))
        if (release.get('applicationId') != APPLICATION_ID or release.get('softwareId') != SOFTWARE_ID
                or release.get('versionName') != VERSION or release.get('channel') != 'stable'
                or release.get('identity', {}).get('certificateSha256') != signing.get('certificateSha256')
                or release.get('identity', {}).get('publisher') != signing.get('publisher')):
            raise SystemExit('Formal source/update/signing identity mismatch')
        if not args.qt_build_metadata or not args.qt_build_metadata.is_file():
            raise SystemExit('Formal Qt build provenance is required')
        qt_meta = json.loads(args.qt_build_metadata.read_text(encoding='utf-8-sig'))
        if (qt_meta.get('stage') != 'formal' or qt_meta.get('version') != VERSION
                or qt_meta.get('qt_plugin_sha256') != sha(args.qt_plugin)
                or qt_meta.get('release_identity_sha256') != sha(release_path)):
            raise SystemExit('Qt plugin does not match the current formal update identity')
    if sha(args.official_zip) != OFFICIAL_SHA256:
        raise SystemExit('Official VLC 3.0.24 ZIP checksum mismatch')
    if not args.qt_plugin.is_file():
        raise SystemExit('Rebuild the Qt interface plugin first')
    runtime = work / ('全能影音播放器' if formal else '全能影音播放器_QA3')
    if runtime.exists() and not args.resume:
        raise SystemExit('Use a new work directory; no existing build is overwritten')
    work.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.official_zip) as archive:
        root = 'vlc-3.0.24/'
        for info in archive.infolist():
            if not info.filename.startswith(root) or info.is_dir():
                continue
            relative = Path(info.filename[len(root):])
            target = (runtime / relative).resolve()
            if not target.is_relative_to(runtime.resolve()):
                raise SystemExit('Unsafe upstream archive entry')
            target.parent.mkdir(parents=True, exist_ok=True)
            if args.resume and target.exists():
                continue
            with archive.open(info) as incoming, target.open('wb') as outgoing:
                shutil.copyfileobj(incoming, outgoing)
    build = work / 'entry-build'
    subprocess.run(['wsl', '-d', 'Ubuntu', '--', 'bash',
                    linux(source / 'haoke/build-entry.sh'), linux(source), linux(build)], check=True)
    crt_check = subprocess.run([str(build / 'test-crt-argc.exe'), '中文', 'argument with spaces'],
                               check=True, capture_output=True, text=True,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    print(crt_check.stdout.strip())
    shutil.copy2(build / 'vlc.exe', runtime / 'vlc.exe')
    shutil.copy2(args.qt_plugin, runtime / 'plugins/gui/libqt_plugin.dll')
    if sha(runtime / 'plugins/gui/libqt_plugin.dll') != sha(args.qt_plugin):
        raise SystemExit('Qt plugin copy mismatch before signing')
    shutil.copytree(source / '依赖/运行库许可', runtime / '第三方许可', dirs_exist_ok=True)
    # The cinema font is embedded in the rebuilt Qt plugin; distribute its OFL.
    shutil.copy2(source / 'haoke/installer_assets/LICENSE.txt',
                 runtime / '第三方许可/NotoSansCJKsc-OFL.txt')
    for upstream_file, installed_file in (
            ('COPYING.LIB', 'COPYING.LIB.txt'), ('AUTHORS', 'AUTHORS.txt'),
            ('THANKS', 'THANKS.txt')):
        shutil.copy2(source / 'vlc-3.0.24' / upstream_file, runtime / installed_file)
    shutil.copy2(source / 'haoke/OPEN_SOURCE_NOTICE.txt', runtime / 'OPEN_SOURCE_NOTICE.txt')
    # Use the upstream portable profile convention: no AppData/registry migration.
    (runtime / 'portable').mkdir(exist_ok=True)
    (runtime / 'portable' / '保留此目录.txt').write_text(
        '本目录保存此浩科版的独立配置。删除会使 VLC 使用系统用户配置。\n', encoding='utf-8')
    for locale, po in catalogs(source, formal=formal).items():
        dest = runtime / 'locale' / locale / 'LC_MESSAGES' / 'vlc.mo'
        dest.parent.mkdir(parents=True, exist_ok=True)
        po.save_as_mofile(str(dest))
    (runtime / ('浩科优化版说明.txt' if formal else '浩科测试版说明.txt')).write_text(
        f'全能影音播放器 / VLC media player {VERSION} ' + ('正式版\n' if formal else 'QA测试版\n') +
        ('通过 Setup.exe 向导安装后运行。配置保存在独立安装目录的 portable 子目录；不注册文件关联。\n' if formal else
         '解压整个目录后运行 vlc.exe。独立便携配置，不注册文件关联。\n') +
        '修改范围：原生语言与参数解析、名称、Qt影院深色界面、默认大窗口和视频比例适配。\n'
        '界面字体 Noto Sans CJK SC，SIL OFL 1.1，允许商用；许可见第三方许可。\n'
        '首次默认工作区约64%面积；播放时按视频显示比例调整，小视频不按像素数缩小。\n'
        '手动尺寸作为后续面积基准；最大化、全屏和有效保存状态优先保留。\n'
        '播放核心与其余364个官方DLL/插件保持不变；Qt界面插件已重建。\n' +
        ('本正式版接入独立身份的签名更新通知；仅用户点击版本页，不自动安装。\n' if formal else
         '本QA入口未签名，未启用浩科正式更新通知。\n') +
        '语言覆盖：系统第一首选 zh 使用简体中文，其余使用英文；启动参数可显式覆盖。\n'
        '中文：--language zh_CN；英文：--language=en_US；跟随系统：--language auto。\n'
        '对应可维护源码、依赖、构建入口和许可证随本发行交付目录的“源码”提供。\n'
        '播放器及浩科修改 GPL-2.0-or-later；LibVLC LGPL-2.1-or-later；第三方组件按各自许可。\n'
        '本版为第三方优化发行，不是 VideoLAN 官方发行或背书；详见 OPEN_SOURCE_NOTICE.txt。\n'
        'VLC、VideoLAN 标识和上游著作权归原权利人所有。\n', encoding='utf-8')
    if formal:
        ps_environment = os.environ.copy()
        ps_environment['PSModulePath'] = os.pathsep.join([
            str(Path(os.environ['WINDIR']) / 'System32/WindowsPowerShell/v1.0/Modules'),
            str(Path(os.environ['ProgramFiles']) / 'WindowsPowerShell/Modules'),
            str(Path.home() / 'Documents/WindowsPowerShell/Modules')])
        subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-File',
                        str(source / 'haoke/sign-windows.ps1'), '-Identity',
                        str(args.signing_metadata.resolve()), '-RuntimeRoot', str(runtime)],
                       env=ps_environment, check=True)
    immutable = []
    with zipfile.ZipFile(args.official_zip) as upstream:
        for info in upstream.infolist():
            relative = info.filename.removeprefix('vlc-3.0.24/')
            if relative.endswith('.dll') or relative.startswith('plugins/'):
                if info.is_dir(): continue
                if relative == 'plugins/gui/libqt_plugin.dll':
                    if not formal and sha(runtime / relative) != sha(args.qt_plugin):
                        raise SystemExit('Qt plugin copy mismatch')
                    continue
                if hashlib.sha256(upstream.read(info)).hexdigest() != sha(runtime / relative):
                    raise SystemExit('Unexpected modification: ' + relative)
                immutable.append(relative)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        raise SystemExit('Output already exists; not overwriting')
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for file in sorted(runtime.rglob('*')):
            if file.is_file():
                # Never ship the QA operator's recent media or window state.
                if file.is_relative_to(runtime / 'portable') and file.name != '保留此目录.txt':
                    continue
                archive.write(file, runtime.name + '/' + file.relative_to(runtime).as_posix())
    print(json.dumps({'runtime': str(runtime), 'output': str(args.output),
                      'sha256': sha(args.output), 'bytes': args.output.stat().st_size,
                      'unchanged_official_modules': len(immutable),
                      'rebuilt_qt_sha256': sha(args.qt_plugin),
                      'packaged_qt_sha256': sha(runtime / 'plugins/gui/libqt_plugin.dll'),
                      'stage':args.stage}, ensure_ascii=False))

if __name__ == '__main__':
    main()
