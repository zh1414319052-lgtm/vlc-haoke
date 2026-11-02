# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
"""Build one signed Windows Setup from an accepted, signed VLC runtime ZIP.

No existing install is touched and no key is created/exported. Work/output paths
must be new. The true Inno wizard, its uninstaller and the native language
bootstrap all reuse the established Authenticode identity.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import runpy
import shutil
import stat
import subprocess
import sys
import zipfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent
INSTALLER_ID = "Haoke.VLC.b92689d5ae8d3982.Windows"
FONT_SHA256 = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"
FONT_NAME = "NotoSansCJKsc-Regular.otf"
MAX_EXPANDED = 1024 * 1024 * 1024
MAX_ENTRIES = 5000
RESERVED = {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(1, 10)), *(f"lpt{i}" for i in range(1, 10))}


class BuildError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def windows_units(value: str) -> int:
    return len(value.encode("utf-16-le")) // 2


def safe_members(archive: zipfile.ZipFile) -> list[tuple[zipfile.ZipInfo, PurePosixPath]]:
    entries = archive.infolist()
    if not entries or len(entries) > MAX_ENTRIES or sum(e.file_size for e in entries) > MAX_EXPANDED:
        raise BuildError("Runtime archive size/count limit exceeded")
    seen, roots, output = set(), set(), []
    for entry in entries:
        name = entry.filename
        if name != entry.orig_filename or "\\" in name or "\x00" in name:
            raise BuildError("Invalid archive name")
        path = PurePosixPath(name)
        if path.is_absolute() or not path.parts or any(
            part in (".", "..", "") or ":" in part or part.endswith((".", " "))
            or part.split(".")[0].casefold() in RESERVED for part in path.parts
        ):
            raise BuildError("Unsafe archive path")
        if str(path) != name.rstrip("/"):
            raise BuildError("Non-canonical archive path")
        mode = entry.external_attr >> 16
        if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)) or entry.flag_bits & 1:
            raise BuildError("Runtime archive contains a link/special/encrypted entry")
        folded = str(path).casefold()
        if folded in seen:
            raise BuildError("Case-colliding/duplicate runtime archive member")
        seen.add(folded)
        roots.add(path.parts[0])
        if len(path.parts) == 1:
            if not entry.is_dir():
                raise BuildError("Top-level runtime entry must be an application directory")
            continue
        relative = PurePosixPath(*path.parts[1:])
        if relative.parts[0].casefold() == "portable":
            continue
        if relative.suffix.casefold() in {".pfx", ".p12", ".keystore", ".key", ".pem", ".mkv", ".mp4", ".env", ".log"}:
            raise BuildError("Runtime archive contains an operator/private/media file")
        output.append((entry, relative))
    if len(roots) != 1:
        raise BuildError("Runtime archive must have one application root")
    return output


def extract_runtime(source: Path, expected: str, destination: Path) -> int:
    if not re.fullmatch(r"[a-fA-F0-9]{64}", expected) or sha256(source) != expected.lower():
        raise BuildError("Accepted runtime ZIP hash mismatch")
    if destination.exists():
        raise BuildError("Staging payload must be absent")
    with zipfile.ZipFile(source) as archive:
        members = safe_members(archive)
        destination.mkdir()
        for entry, relative in members:
            target = destination.joinpath(*relative.parts)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(entry) as src, target.open("xb") as dst:
                    shutil.copyfileobj(src, dst)
    if sha256(source) != expected.lower():
        raise BuildError("Runtime input changed while extracting")
    for required in ("vlc.exe", "libvlc.dll", "plugins/gui/libqt_plugin.dll", "COPYING.txt"):
        if not (destination / required).is_file():
            raise BuildError(f"Runtime input is missing {required}")
    if (destination / "portable").exists():
        raise BuildError("Operator profile must not be packaged")
    longest = max(windows_units(str(relative).replace("/", "\\")) for entry, relative in members if not entry.is_dir())
    # VLC portable detection itself uses MAX_PATH. Include the longest known
    # isolated update-state name as well as deployed file paths in this budget.
    profile_longest = windows_units("portable\\vlc\\haoke-updates-v2-" + "f" * 64 + ".json")
    return min(180, 258 - 1 - max(longest, profile_longest))


def file_version(path: Path) -> str:
    if os.name != "nt":
        raise BuildError("Setup production build runs on Windows")
    dll = ctypes.WinDLL("version", use_last_error=True)
    dll.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    dll.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    dll.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    dll.GetFileVersionInfoW.restype = wintypes.BOOL
    dll.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    dll.VerQueryValueW.restype = wintypes.BOOL
    ignored = wintypes.DWORD()
    length = dll.GetFileVersionInfoSizeW(str(path), ctypes.byref(ignored))
    if not length:
        raise BuildError("Runtime executable lacks native file-version resources")
    data = ctypes.create_string_buffer(length)
    if not dll.GetFileVersionInfoW(str(path), 0, length, data):
        raise BuildError("Cannot read runtime file-version resources")
    pointer, size = ctypes.c_void_p(), wintypes.UINT()
    if not dll.VerQueryValueW(data, "\\", ctypes.byref(pointer), ctypes.byref(size)) or size.value < 52:
        raise BuildError("Invalid native file version")
    fields = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD))
    return ".".join(str(n) for n in (fields[2] >> 16, fields[2] & 0xFFFF, fields[3] >> 16, fields[3] & 0xFFFF))


def wsl_path(path: Path) -> str:
    path = path.resolve()
    if not re.match(r"^[A-Za-z]:\\", str(path)):
        raise BuildError("Build input must be on a local Windows drive")
    return "/mnt/" + path.drive[0].lower() + str(path)[2:].replace("\\", "/")


def run(command: list[str], logfile: Path, env: dict[str, str]) -> str:
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    logfile.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        print((result.stdout + result.stderr)[-5000:])
        raise BuildError(f"Build command failed; see {logfile}")
    return result.stdout


def rc_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def resource_text(payload: Path, icon: Path, version: str, filever: str) -> str:
    numbers = filever.replace(".", ",")
    blocks = []
    for language, description, product in (("040904b0", "VLC media player - Haoke setup", "VLC media player (Haoke)"),
                                           ("080404b0", "全能影音播放器安装程序", "全能影音播放器")):
        fields = {"CompanyName": "Haoke Tools (VLC Optimized Edition)", "FileDescription": description,
                  "FileVersion": filever, "InternalName": "HaokeVLCSetup", "OriginalFilename": "Setup.exe",
                  "ProductName": product, "ProductVersion": version,
                  "LegalCopyright": "VideoLAN contributors; Haoke edition. GPL-2.0-or-later."}
        values = "\n".join(f'      VALUE "{k}", "{rc_string(v)}\\0"' for k, v in fields.items())
        blocks.append(f'    BLOCK "{language}"\n    BEGIN\n{values}\n    END')
    return f'''#pragma code_page(65001)
#include <windows.h>
101 RCDATA "{rc_string(wsl_path(payload))}"
1 ICON "{rc_string(wsl_path(icon))}"
1 VERSIONINFO
 FILEVERSION {numbers}
 PRODUCTVERSION {numbers}
 FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
 FILEFLAGS 0
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
{chr(10).join(blocks)}
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x0409, 1200, 0x0804, 1200
  END
END
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-zip", type=Path, required=True)
    parser.add_argument("--runtime-sha256", required=True)
    parser.add_argument("--signing-metadata", type=Path, required=True, help="External public identity metadata only")
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=Path(r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"))
    args = parser.parse_args()
    work, output = args.work_dir.resolve(), args.output.resolve()
    if os.name != "nt" or not args.compiler.is_file():
        raise BuildError("Windows with an installed Inno Setup compiler is required")
    if work.exists() or output.exists() or not output.name.lower().endswith("setup.exe"):
        raise BuildError("Work/output must be new and the final filename must end in Setup.exe")
    if not work.parent.is_dir() or not output.parent.is_dir():
        raise BuildError("Explicit work/output parent directories must exist")
    version = runpy.run_path(str(HERE / "version.py"))
    identity = json.loads(args.signing_metadata.read_text(encoding="utf-8-sig"))
    release = json.loads((HERE / "release_identity.json").read_text(encoding="utf-8-sig"))
    if (identity.get("applicationId") != version["APPLICATION_ID"]
        or release.get("identity", {}).get("certificateSha256") != identity.get("certificateSha256")
        or release.get("identity", {}).get("publisher") != identity.get("publisher")
        or release.get("versionName") != version["VERSION"] or release.get("versionCode") != version["VERSION_CODE"]):
        raise BuildError("Current source/update/signing public identity mismatch")
    work.mkdir()
    for name in ("logs", "assets", "inno-output", "signed-uninstaller", "compiler-temp", "bootstrap"):
        (work / name).mkdir()
    payload, assets = work / "payload", work / "assets"
    max_path = extract_runtime(args.runtime_zip.resolve(), args.runtime_sha256, payload)
    if file_version(payload / "vlc.exe") != version["FILE_VERSION"]:
        raise BuildError("Runtime executable version does not match current installer source")
    (payload / "浩科优化版说明.txt").write_text(
        f"全能影音播放器 / VLC media player (Haoke) {version['VERSION']}\n"
        "Windows x64 安装向导版。通过本次签名 Setup.exe 安装，安装目录须为当前用户可写的本地目录。\n"
        "默认安装在当前用户 LocalAppData\\Programs\\HaokeVLC，可在向导中选择其他空的可写目录。\n"
        "安装器创建独立 portable 目录；播放器、Qt设置及版本通知状态均在该目录内，不覆盖原版VLC配置。\n"
        "同身份覆盖安装保留现有配置和用户数据；系统卸载仅移除安装清单文件，保留portable及产品标记以便重装。\n"
        "签名为本产品独立自签名身份，不代表获得公众CA信任。完整对应源码、依赖源码与许可随源码目录提供。\n"
        "This is the per-user Windows x64 Setup edition. Choose a writable local installation folder.\n"
        "An empty portable profile is created at installation; upgrades and standard uninstall preserve user data.\n"
        "The official VLC installation/profile remains separate. Preserve the corresponding source and licenses.\n",
        encoding="utf-8")
    font_source = HERE / "installer_assets"
    if sha256(font_source / FONT_NAME) != FONT_SHA256:
        raise BuildError("Commercial font does not match the retained official source")
    for name in (FONT_NAME, "LICENSE.txt", "source.json"):
        shutil.copy2(font_source / name, assets / name)
    icon = SOURCE / "vlc-3.0.24/share/icons/vlc.ico"
    shutil.copy2(icon, assets / "vlc.ico")
    (assets / "haoke-install.ini").write_text("[HaokeInstall]\nProductId=" + INSTALLER_ID + "\nProfile=portable\n", encoding="utf-8")
    env = os.environ.copy()
    env["PSModulePath"] = os.pathsep.join([str(Path(os.environ["WINDIR"]) / "System32/WindowsPowerShell/v1.0/Modules"),
        str(Path(os.environ["ProgramFiles"]) / "WindowsPowerShell/Modules"), str(Path.home() / "Documents/WindowsPowerShell/Modules")])
    env["TEMP"] = env["TMP"] = str(work / "compiler-temp")
    powershell = str(Path(os.environ["WINDIR"]) / "System32/WindowsPowerShell/v1.0/powershell.exe")
    signer = HERE / "sign-artifact.ps1"
    common_sign = [powershell, "-NoProfile", "-NonInteractive", "-File", str(signer), "-Identity",
                   str(args.signing_metadata.resolve()), "-AllowedRoot", str(work)]
    for relative in ("vlc.exe", "plugins/gui/libqt_plugin.dll"):
        run([*common_sign, "-TargetFile", str(payload / relative), "-VerifyOnly"], work / "logs" / (Path(relative).name + ".verify.log"), env)
    original_signed_hashes = {name: sha256(payload / name) for name in ("vlc.exe", "plugins/gui/libqt_plugin.dll")}
    sign_command = (f'$q{powershell}$q -NoProfile -NonInteractive -File $q{signer}$q '
                    f'-Identity $q{args.signing_metadata.resolve()}$q -AllowedRoot $q{work}$q -TargetFile $f')
    definitions = {"PayloadDir": str(payload), "AssetsDir": str(assets), "AppVersion": version["VERSION"],
                   "FileVersion": version["FILE_VERSION"], "OutputName": "HaokeVLCWizard",
                   "MaxAppPath": str(max_path), "SignedDir": str(work / "signed-uninstaller")}
    command = [str(args.compiler), *[f"/D{k}={v}" for k, v in definitions.items()],
               "/O" + str(work / "inno-output"), "/Shaoke_vlc_setup=" + sign_command, str(HERE / "installer.iss")]
    print("Building and signing the real installation wizard/uninstaller …", flush=True)
    run(command, work / "logs/inno-compile.log", env)
    inner = work / "inno-output/HaokeVLCWizard.exe"
    if not inner.is_file():
        raise BuildError("Inno compiler did not produce its signed wizard")
    run([*common_sign, "-TargetFile", str(inner), "-VerifyOnly"], work / "logs/wizard.verify.log", env)
    uninst = list((work / "signed-uninstaller").glob("*.e32")) + list((work / "signed-uninstaller").glob("*.e64"))
    if not uninst:
        raise BuildError("Signed uninstaller build evidence is absent")
    for index, item in enumerate(uninst):
        run([*common_sign, "-TargetFile", str(item), "-VerifyOnly"], work / f"logs/uninstaller-{index}.verify.log", env)
    boot = work / "bootstrap"
    inner_sha = sha256(inner)
    (boot / "setup_payload_hash.h").write_text(f'#define HAOKE_SETUP_PAYLOAD_SHA256 "{inner_sha}"\n#define HAOKE_SETUP_PAYLOAD_SIZE {inner.stat().st_size}UL\n', encoding="ascii")
    resource = boot / "setup_resources.rc"
    resource.write_text(resource_text(inner, icon, version["VERSION"], version["FILE_VERSION"]), encoding="utf-8")
    obj = boot / "setup_resources.o"
    print("Building the native first-preferred-language Setup entry …", flush=True)
    run(["wsl.exe", "-d", "Ubuntu", "--", "x86_64-w64-mingw32-windres", "-i", wsl_path(resource), "-o", wsl_path(obj)], work / "logs/bootstrap-resource.log", env)
    built = boot / "Setup.exe"
    run(["wsl.exe", "-d", "Ubuntu", "--", "x86_64-w64-mingw32-gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-municode", "-mwindows", "-static", "-I" + wsl_path(HERE), "-I" + wsl_path(boot), wsl_path(HERE / "setup_bootstrap.c"),
         wsl_path(obj), "-o", wsl_path(built), "-lbcrypt", "-lshell32", "-ladvapi32", "-Wl,--nxcompat,--dynamicbase,--high-entropy-va"],
        work / "logs/bootstrap-build.log", env)
    run([*common_sign, "-TargetFile", str(built)], work / "logs/setup-signature.log", env)
    if any(sha256(payload / name) != before for name, before in original_signed_hashes.items()):
        raise BuildError("Accepted application signatures were unexpectedly changed")
    if sha256(args.runtime_zip) != args.runtime_sha256.lower():
        raise BuildError("Accepted runtime ZIP changed during installer construction")
    shutil.copy2(built, output)
    if sha256(built) != sha256(output):
        raise BuildError("Final installer copy hash mismatch")
    receipt = {"output": str(output), "sha256": sha256(output), "bytes": output.stat().st_size,
               "versionName": version["VERSION"], "fileVersion": version["FILE_VERSION"], "versionCode": version["VERSION_CODE"],
               "applicationId": version["APPLICATION_ID"], "installerAppId": INSTALLER_ID,
               "runtimeZipSha256": args.runtime_sha256.lower(), "innerWizardSha256": inner_sha,
               "bootstrapLanguagePolicy": "GetUserPreferredUILanguages first entry; any zh maps to zh_CN; others en_US; explicit /LANG or --language supported",
               "installerScope": "current-user; no elevation", "defaultInstallDirectory": "{localappdata}/Programs/HaokeVLC",
               "configuration": "installer creates an empty portable directory; user data and product marker survive uninstall",
               "maxAppPathUtf16Units": max_path, "signerCertificateSha256": identity["certificateSha256"],
               "publisher": identity["publisher"], "signatureVerification": "outer/wizard/uninstaller pinned Authenticode digest passed; self-signed public trust not claimed",
               "privateKeyMaterialPackaged": False, "font": "Noto Sans CJK SC; SIL OFL1.1; privately loaded, not globally installed",
               "uninstallerBuildFiles": [p.name for p in uninst], "applicationHashesUnchanged": original_signed_hashes,
               "runtimeAcceptance": "pending root GUI installation/upgrade/uninstall acceptance"}
    (work / "setup-receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(receipt, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BuildError, OSError, ValueError, zipfile.BadZipFile) as error:
        raise SystemExit(str(error))
