import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = Path(sys.argv.pop()).resolve()


class DesktopPlaces(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.home = Path(self.temporary.name)
        self.config = self.home / "config"
        self.config.mkdir()
        self.env = dict(os.environ, HOME=str(self.home), XDG_CONFIG_HOME=str(self.config),
                        XDG_DATA_HOME=str(self.home / "data"))

    def run_helper(self):
        return subprocess.run([sys.executable, str(HELPER)], env=self.env,
                              text=True, capture_output=True)

    def test_fresh_and_idempotent(self):
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr)
        desktop = self.home / "Desktop"
        self.assertTrue(desktop.is_dir())
        self.assertEqual(result.stdout.strip(), str(desktop))
        private_disk = self.home / "data/zacos9/Zacintosh HD"
        self.assertTrue((private_disk / "System Folder").is_dir())
        self.assertTrue((private_disk / "Applications").is_dir())
        self.assertEqual(private_disk.stat().st_mode & 0o777, 0o700)
        dirs = self.config / "user-dirs.dirs"
        bookmarks = self.config / "gtk-3.0/bookmarks"
        self.assertEqual(dirs.read_text(), 'XDG_DESKTOP_DIR="$HOME/Desktop"\n')
        self.assertEqual(bookmarks.read_text(), desktop.as_uri() + " Desktop\n" +
                         private_disk.as_uri() + " Zacintosh HD\n")
        before = (dirs.read_bytes(), bookmarks.read_bytes())
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertEqual(before, (dirs.read_bytes(), bookmarks.read_bytes()))

    def test_custom_desktop_and_existing_bookmarks(self):
        desktop = self.home / "My Desktop #1"
        dirs = self.config / "user-dirs.dirs"
        original = '# Keep this\nXDG_DOWNLOAD_DIR="$HOME/Downloads"\n' \
                   'XDG_DESKTOP_DIR="${HOME}/My Desktop #1"'
        dirs.write_text(original)
        bookmarks = self.config / "gtk-3.0/bookmarks"
        bookmarks.parent.mkdir()
        bookmarks.write_text("file:///tmp Existing")
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dirs.read_text(), original)
        self.assertEqual(bookmarks.read_text(),
                         "file:///tmp Existing\n" + desktop.as_uri() + " Desktop\n" +
                         (self.home / "data/zacos9/Zacintosh HD").as_uri() +
                         " Zacintosh HD\n")
        self.assertEqual(result.stdout.strip(), str(desktop))

    def test_existing_desktop_label_preserved(self):
        bookmarks = self.config / "gtk-3.0/bookmarks"
        bookmarks.parent.mkdir()
        original = (self.home / "Desktop").as_uri() + " My Work\n"
        bookmarks.write_text(original)
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertEqual(bookmarks.read_text(), original +
                         (self.home / "data/zacos9/Zacintosh HD").as_uri() +
                         " Zacintosh HD\n")

    def test_missing_desktop_entry_preserves_other_settings(self):
        dirs = self.config / "user-dirs.dirs"
        dirs.write_text('XDG_DOCUMENTS_DIR="$HOME/Papers"')
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertEqual(dirs.read_text(),
                         'XDG_DOCUMENTS_DIR="$HOME/Papers"\nXDG_DESKTOP_DIR="$HOME/Desktop"\n')

    def test_invalid_paths_are_reported_without_rewriting(self):
        dirs = self.config / "user-dirs.dirs"
        for setting in ('"relative/path"', '"$UNSUPPORTED/Desktop"', '"unterminated'):
            original = f"XDG_DESKTOP_DIR={setting}\n"
            dirs.write_text(original)
            result = self.run_helper()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("zacos9-desktop-places:", result.stderr)
            self.assertEqual(dirs.read_text(), original)

    def test_unusable_directory_is_reported(self):
        (self.home / "Desktop").write_text("not a folder")
        result = self.run_helper()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("zacos9-desktop-places:", result.stderr)
        self.assertFalse((self.config / "gtk-3.0/bookmarks").exists())
        self.assertFalse((self.config / "user-dirs.dirs").exists())

    def test_symlinked_config_files_preserved(self):
        real_dirs = self.home / "managed-user-dirs"
        real_dirs.write_text('XDG_DOCUMENTS_DIR="$HOME/Documents"\n')
        (self.config / "user-dirs.dirs").symlink_to(real_dirs)
        bookmarks = self.config / "gtk-3.0/bookmarks"
        bookmarks.parent.mkdir()
        real_bookmarks = self.home / "managed-bookmarks"
        real_bookmarks.write_text("file:///tmp Existing\n")
        real_bookmarks.chmod(0o640)
        bookmarks.symlink_to(real_bookmarks)
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertTrue(bookmarks.is_symlink())
        self.assertTrue((self.config / "user-dirs.dirs").is_symlink())
        self.assertEqual(real_bookmarks.stat().st_mode & 0o777, 0o640)
        self.assertIn('XDG_DESKTOP_DIR="$HOME/Desktop"\n', real_dirs.read_text())
        self.assertIn((self.home / "Desktop").as_uri() + " Desktop\n", real_bookmarks.read_text())
        self.assertIn((self.home / "data/zacos9/Zacintosh HD").as_uri() +
                      " Zacintosh HD\n", real_bookmarks.read_text())

    def test_private_workspace_symlink_is_rejected(self):
        target = self.home / "external"
        target.mkdir()
        target.chmod(0o755)
        (self.home / "data/zacos9").mkdir(parents=True)
        (self.home / "data/zacos9/Zacintosh HD").symlink_to(target, target_is_directory=True)
        result = self.run_helper()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Refusing to use a symlink", result.stderr)
        self.assertEqual(target.stat().st_mode & 0o777, 0o755)
        self.assertEqual(list(target.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
