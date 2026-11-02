"""Focused installer-input acceptance checks, without signing/installing."""
import importlib.util
import io
from pathlib import Path
import stat
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("haoke_build_setup", Path(__file__).parents[1] / "build_setup.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class SetupInputTests(unittest.TestCase):
    def members(self, names):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as archive:
            for name in names:
                if isinstance(name, zipfile.ZipInfo):
                    archive.writestr(name, "test")
                else:
                    archive.writestr(name, "" if name.endswith("/") else "test")
        stream.seek(0)
        return zipfile.ZipFile(stream)

    def test_profiles_excluded_without_discarding_application_files(self):
        with self.members(["Player/", "Player/vlc.exe", "Player/plugins/gui/libqt_plugin.dll",
                           "Player/portable/", "Player/portable/vlc/vlcrc"]) as archive:
            accepted = builder.safe_members(archive)
        self.assertEqual([str(p) for _, p in accepted], ["vlc.exe", "plugins/gui/libqt_plugin.dll"])

    def test_windows_path_collisions_and_traversal_rejected(self):
        invalid = ["Player/../vlc.exe", "Player/foo:stream", "Player/foo.", "Player/foo ",
                   "Player/CON.txt", "Player/LPT1", "Player//bad", "C:/Player/vlc.exe", "Player\\vlc.exe"]
        for name in invalid:
            with self.subTest(name=name), self.members(["Player/vlc.exe", name]) as archive:
                with self.assertRaises(builder.BuildError):
                    builder.safe_members(archive)
        with self.members(["Player/vlc.exe", "Player/VLC.EXE"]) as archive:
            with self.assertRaises(builder.BuildError): builder.safe_members(archive)
        with self.members(["Player/vlc.exe", "Other/file"]) as archive:
            with self.assertRaises(builder.BuildError): builder.safe_members(archive)

    def test_link_and_operator_material_rejected(self):
        link = zipfile.ZipInfo("Player/link")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for bad in [link, "Player/private.pfx", "Player/02.mkv", "Player/operator.env"]:
            with self.subTest(bad=str(bad)), self.members(["Player/vlc.exe", bad]) as archive:
                with self.assertRaises(builder.BuildError): builder.safe_members(archive)

    def test_utf16_budget_and_resource_version_keep_real_identifiers(self):
        self.assertEqual(builder.windows_units("路径"), 2)
        self.assertEqual(builder.windows_units("😀"), 2)
        text = builder.resource_text(Path(r"D:\test\wizard.exe"), Path(r"D:\test\vlc.ico"), "3.0.24-Haoke.4", "3.0.24.4")
        self.assertIn("FILEVERSION 3,0,24,4", text)
        self.assertIn('BLOCK "040904b0"', text)
        self.assertIn('BLOCK "080404b0"', text)
        self.assertIn("3.0.24-Haoke.4", text)


if __name__ == "__main__":
    unittest.main()
