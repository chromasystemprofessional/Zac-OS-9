"""Dependency-mocked tests; run with the helper's path as the first argument."""

import contextlib
import importlib.machinery
import importlib.util
import io
import json
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import Mock, call, patch


helper_path = (sys.argv.pop(1) if len(sys.argv) > 1 else
               str(Path(__file__).resolve().parents[2] / "appstore/zacos9-software-catalog"))
loader = importlib.machinery.SourceFileLoader("software_catalog", helper_path)
spec = importlib.util.spec_from_loader(loader.name, loader)
catalog = importlib.util.module_from_spec(spec)
loader.exec_module(catalog)


def component(identifier="org.example.Editor", kind="desktop", packages=None,
              name="Éditeur", summary="Edit documents"):
    return SimpleNamespace(
        get_id=lambda: identifier, get_kind=lambda: kind,
        get_pkgnames=lambda: packages, get_name=lambda: name,
        get_summary=lambda: summary,
    )


class CatalogTests(unittest.TestCase):
    def setUp(self):
        metadata = patch.object(catalog, "flathub_metadata", return_value={})
        self.metadata = metadata.start()
        self.addCleanup(metadata.stop)

    def flatpak_results(self, result):
        return [
            SimpleNamespace(returncode=0, stderr="", stdout=(
                "flathub\thttps://dl.flathub.org/repo/\t\n"
            )),
            result,
        ]

    def appstream(self, components, loaded=True):
        pool = Mock()
        pool.load.return_value = loaded
        pool.get_components.return_value.as_array.return_value = components
        appstream = SimpleNamespace(
            Pool=SimpleNamespace(new=lambda: pool),
            PoolFlags=SimpleNamespace(LOAD_OS_CATALOG=1),
            ComponentKind=SimpleNamespace(DESKTOP_APP="desktop"),
        )
        gi = ModuleType("gi")
        gi.require_version = Mock()
        repository = ModuleType("gi.repository")
        repository.AppStream = appstream
        return pool, patch.dict(sys.modules, {"gi": gi, "gi.repository": repository})

    def invoke(self, args):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            status = catalog.main(args)
        return status, stdout.getvalue(), stderr.getvalue()

    def test_debian_only_packaged_gui_apps(self):
        pool, modules = self.appstream([
            component(packages=["editor-data", "editor", "editor", "--unsafe", "Bad"]),
            component("org.example.Console", kind="console", packages=["console"]),
            component("org.example.Flatpak", packages=[]),
            component("org.example.Unowned", packages=None),
            component("org.example.Unsafe", packages=["-bad", "bad name", "x"]),
            component(None, packages=["editor"]),
            component(packages=["duplicate"]),
        ])
        with modules:
            items = catalog.debian_catalog()
        self.assertEqual(items, [{
            "id": "org.example.Editor", "name": "Éditeur", "blurb": "Edit documents",
            "packages": ["editor", "editor-data"], "source": "debian",
        }])
        pool.set_flags.assert_called_once_with(1)
        pool.load.assert_called_once_with(None)

    def test_debian_missing_optional_fields_and_order(self):
        _, modules = self.appstream([
            component("z.editor", packages=["z-editor"], name=None, summary=None),
            component("a.editor", packages=["a-editor"]),
        ])
        with modules:
            items = catalog.debian_catalog()
        self.assertEqual([item["id"] for item in items], ["a.editor", "z.editor"])
        self.assertEqual(items[1]["name"], "z.editor")
        self.assertEqual(items[1]["blurb"], "")

    def test_debian_empty_catalog(self):
        _, modules = self.appstream([])
        with modules:
            self.assertEqual(catalog.debian_catalog(), [])

    def test_debian_missing_bindings(self):
        with patch.dict(sys.modules, {"gi": None}):
            status, stdout, stderr = self.invoke(["debian"])
        self.assertEqual(status, 1)
        self.assertEqual(stdout, "")
        self.assertIn("gir1.2-appstream-1.0", stderr)

    def test_debian_load_failures(self):
        for failure in (False, RuntimeError("bad metadata")):
            pool, modules = self.appstream([], loaded=failure)
            if isinstance(failure, Exception):
                pool.load.side_effect = failure
            with modules:
                status, stdout, stderr = self.invoke(["debian"])
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("Debian AppStream catalog", stderr)

    def test_flathub_command_and_schema(self):
        result = SimpleNamespace(returncode=0, stderr="", stdout=(
            "org.example.Writer\tWriter\tWrite documents\n"
            "org.example.Editor\tÉditeur\tEdit documents\n"
            "org.example.Editor\tDuplicate\tIgnored\n"
        ))
        with patch.object(catalog.subprocess, "run",
                          side_effect=self.flatpak_results(result)) as run:
            status, stdout, stderr = self.invoke(["flathub"])
        self.assertEqual(run.call_args_list, [
            call(["flatpak", "--user", "remote-list", "--show-disabled",
                  "--columns=name,url,options"],
                 capture_output=True, text=True, check=False, timeout=60),
            call(["flatpak", "--user", "remote-ls", "--app",
                  "--columns=application,name,description", "flathub"],
                 capture_output=True, text=True, check=False, timeout=60),
        ])
        self.assertEqual(status, 0)
        self.assertEqual(stderr, "")
        document = json.loads(stdout)
        self.assertEqual(set(document), {"items"})
        items = document["items"]
        self.assertTrue(all(isinstance(item["source"], str) for item in items))
        self.assertEqual(items[0], {
            "id": "org.example.Editor", "name": "Éditeur", "blurb": "Edit documents",
            "packages": ["org.example.Editor"], "source": "flathub",
            "category": "Utilities",
        })
        self.assertEqual(len(items), 2)

    def test_debian_json_object_contract(self):
        _, modules = self.appstream([component(packages=["editor"])])
        with modules:
            status, stdout, stderr = self.invoke(["debian"])
        self.assertEqual(status, 0)
        self.assertEqual(stderr, "")
        document = json.loads(stdout)
        self.assertEqual(set(document), {"items"})
        self.assertEqual(document["items"][0]["source"], "debian")

    def test_empty_json_object_contract(self):
        _, modules = self.appstream([])
        with modules:
            status, stdout, stderr = self.invoke(["debian"])
        self.assertEqual(status, 0)
        self.assertEqual(stderr, "")
        self.assertEqual(json.loads(stdout), {"items": []})

    def test_flathub_invalid_ids_filtered(self):
        identifiers = [
            "--system", "org.example", "org..Editor", "1org.example.Editor",
            "org.bad-name.Editor", "org.example.Editor/stable",
            "org.example.Editor;touch file", "org.example." + "a" * 256,
        ]
        output = "".join(f"{identifier}\tBad\tBad\n" for identifier in identifiers)
        output += "org.example.Valid-app\t\t\n"
        result = SimpleNamespace(returncode=0, stderr="", stdout=output)
        with patch.object(catalog.subprocess, "run",
                          side_effect=self.flatpak_results(result)):
            items = catalog.flathub_catalog()
        self.assertEqual([item["id"] for item in items], ["org.example.Valid-app"])
        self.assertEqual(items[0]["name"], "org.example.Valid-app")
        self.assertEqual(items[0]["blurb"], "")

    def test_flathub_empty(self):
        result = SimpleNamespace(returncode=0, stderr="", stdout="\n")
        with patch.object(catalog.subprocess, "run",
                          side_effect=self.flatpak_results(result)):
            self.assertEqual(catalog.flathub_catalog(), [])

    def test_flathub_description_column_may_be_omitted(self):
        result = SimpleNamespace(returncode=0, stderr="",
                                 stdout="org.example.Editor\tEditor\n")
        with patch.object(catalog.subprocess, "run",
                          side_effect=self.flatpak_results(result)):
            items = catalog.flathub_catalog()
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["name"], "Editor")
        self.assertEqual(items[0]["blurb"], "")

    def test_flathub_enriched_metadata_and_desktop_suffix(self):
        self.metadata.return_value = {
            "org.example.Editor.desktop": {
                "name": "Photo Editor", "blurb": "Retouch photographs",
                "category": "Graphics", "icon": "/cache/editor.png",
            },
        }
        result = SimpleNamespace(returncode=0, stderr="",
                                 stdout="org.example.Editor\tEditor\n")
        with patch.object(catalog.subprocess, "run",
                          side_effect=self.flatpak_results(result)):
            item = catalog.flathub_catalog()[0]
        self.assertEqual(item["name"], "Photo Editor")
        self.assertEqual(item["blurb"], "Retouch photographs")
        self.assertEqual(item["category"], "Graphics")
        self.assertEqual(item["icon"], "/cache/editor.png")
        self.assertEqual(item["packages"], ["org.example.Editor"])

    def test_flathub_metadata_failure_is_reported(self):
        self.metadata.side_effect = catalog.Failure("metadata download failed")
        result = SimpleNamespace(returncode=0, stderr="",
                                 stdout="org.example.Editor\tEditor\n")
        with patch.object(catalog.subprocess, "run",
                          side_effect=self.flatpak_results(result)):
            status, stdout, stderr = self.invoke(["flathub"])
        self.assertEqual(status, 1)
        self.assertEqual(stdout, "")
        self.assertIn("metadata download failed", stderr)

    def test_flathub_malformed_rows_fail_without_partial_json(self):
        result = SimpleNamespace(
            returncode=0, stderr="", stdout="org.example.Good\tGood\tWorks\nbad row\n",
        )
        with patch.object(catalog.subprocess, "run",
                          side_effect=self.flatpak_results(result)):
            status, stdout, stderr = self.invoke(["flathub"])
        self.assertEqual(status, 1)
        self.assertEqual(stdout, "")
        self.assertIn("invalid catalog row", stderr)

    def test_flathub_command_failures(self):
        for error, expected in [
            (FileNotFoundError("flatpak missing"), "flatpak missing"),
            (subprocess.TimeoutExpired("flatpak", 60), "timed out"),
        ]:
            with patch.object(catalog.subprocess, "run", side_effect=error):
                status, stdout, stderr = self.invoke(["flathub"])
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(expected, stderr)

    def test_flathub_remote_validation(self):
        for output, expected in [
            ("", "must exist"),
            ("flathub\thttps://evil.example/repo/\t\n", "does not use"),
            ("flathub\thttps://dl.flathub.org/repo//\t\n", "does not use"),
            ("flathub\thttps://dl.flathub.org/repo/\tdisabled\n", "disabled"),
            ("flathub\thttps://dl.flathub.org/repo/\tno-gpg-verify,disabled\n", "disabled"),
            ("flathub\tbad row\n", "does not use"),
            ("flathub\n", "invalid remote row"),
            ("flathub\thttps://evil.example/repo/\n", "does not use"),
            ("flathub\thttps://dl.flathub.org/repo/\t\tunexpected\n", "invalid remote row"),
            ("flathub\thttps://dl.flathub.org/repo/\t\n" * 2, "must exist"),
        ]:
            with self.subTest(output=output):
                result = SimpleNamespace(returncode=0, stderr="", stdout=output)
                with patch.object(catalog.subprocess, "run", return_value=result) as run:
                    status, stdout, stderr = self.invoke(["flathub"])
                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(expected, stderr)
                run.assert_called_once()

    def test_flathub_official_url_without_slash(self):
        results = [
            SimpleNamespace(returncode=0, stderr="", stdout=(
                "other\thttps://example.org/\tdisabled\n"
                "flathub\thttps://dl.flathub.org/repo\t\n"
            )),
            SimpleNamespace(returncode=0, stderr="", stdout=""),
        ]
        with patch.object(catalog.subprocess, "run", side_effect=results):
            self.assertEqual(catalog.flathub_catalog(), [])

    def test_flathub_empty_options_column_may_be_omitted(self):
        for url in ("https://dl.flathub.org/repo/", "https://dl.flathub.org/repo"):
            with self.subTest(url=url):
                results = [
                    SimpleNamespace(returncode=0, stderr="", stdout=f"flathub\t{url}\n"),
                    SimpleNamespace(returncode=0, stderr="", stdout=""),
                ]
                with patch.object(catalog.subprocess, "run", side_effect=results):
                    self.assertEqual(catalog.flathub_catalog(), [])

    def test_flathub_listing_failures_after_validation(self):
        for result in (
            SimpleNamespace(returncode=1, stderr="listing failed", stdout=""),
            subprocess.TimeoutExpired("flatpak", 60),
        ):
            with patch.object(catalog.subprocess, "run",
                              side_effect=self.flatpak_results(result)):
                status, stdout, stderr = self.invoke(["flathub"])
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertTrue(stderr)

    def test_flathub_nonzero_status(self):
        for detail, expected in [("No such remote: flathub", "No such remote"),
                                 ("", "exit status 1")]:
            result = SimpleNamespace(returncode=1, stderr=detail, stdout="ignored")
            with patch.object(catalog.subprocess, "run", return_value=result):
                status, stdout, stderr = self.invoke(["flathub"])
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(expected, stderr)

    def test_invalid_arguments(self):
        for args in ([], ["unknown"], ["debian", "extra"], ["--help"]):
            with patch.object(catalog, "debian_catalog") as debian, \
                    patch.object(catalog, "flathub_catalog") as flathub:
                status, stdout, stderr = self.invoke(args)
            self.assertEqual(status, 2)
            self.assertEqual(stdout, "")
            self.assertIn("debian|flathub", stderr)
            debian.assert_not_called()
            flathub.assert_not_called()


class MetadataTests(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.directory = Path(self.root.name) / "appstream/flathub/x86_64/active"
        self.directory.mkdir(parents=True)
        environment = patch.dict(catalog.os.environ, {"FLATPAK_USER_DIR": self.root.name})
        environment.start()
        self.addCleanup(environment.stop)
        run = patch.object(catalog, "run_flatpak", return_value="")
        self.run = run.start()
        self.addCleanup(run.stop)

    def fixture(self, icon="org.example.Editor.png"):
        return f"""<?xml version="1.0"?>
<components version="0.16" origin="flathub">
  <component type="desktop-application">
    <id>org.example.Editor</id><name>Photo Editor</name>
    <summary>Retouch photographs</summary>
    <categories><category>Graphics</category><category>Utility</category></categories>
    <icon type="cached" width="128" height="128">{icon}</icon>
    <icon type="cached" width="64" height="64">small.png</icon>
  </component>
  <component type="runtime"><id>org.example.Runtime</id></component>
</components>"""

    def test_cached_metadata_and_icons(self):
        (self.directory / "appstream.xml").write_text(self.fixture())
        icons = self.directory / "icons/128x128"
        icons.mkdir(parents=True)
        (icons / "org.example.Editor.png").write_bytes(b"fixture")
        items = catalog.flathub_metadata()
        self.run.assert_called_once_with(
            ["update", "--appstream", "--noninteractive", "flathub"])
        self.assertEqual(items, {"org.example.Editor": {
            "name": "Photo Editor", "blurb": "Retouch photographs",
            "category": "Graphics", "icon": str(icons / "org.example.Editor.png"),
        }})

    def test_compressed_metadata_and_smaller_icon(self):
        (self.directory / "appstream.xml.gz").write_bytes(gzip.compress(self.fixture().encode()))
        icons = self.directory / "icons/64x64"
        icons.mkdir(parents=True)
        (icons / "small.png").write_bytes(b"fixture")
        item = catalog.flathub_metadata()["org.example.Editor"]
        self.assertEqual(item["icon"], str(icons / "small.png"))

    def test_missing_and_malformed_metadata(self):
        with self.assertRaisesRegex(catalog.Failure, "metadata is missing"):
            catalog.flathub_metadata()
        (self.directory / "appstream.xml").write_text("<broken")
        with self.assertRaisesRegex(catalog.Failure, "Flathub AppStream metadata"):
            catalog.flathub_metadata()

    def test_icons_cannot_escape_cache(self):
        for name in ("../outside.png", "/tmp/outside.png", "escape.png"):
            with self.subTest(name=name):
                (self.directory / "appstream.xml").write_text(self.fixture(name))
                icons = self.directory / "icons/128x128"
                icons.mkdir(parents=True, exist_ok=True)
                outside = Path(self.root.name) / "outside.png"
                outside.write_bytes(b"fixture")
                if name == "escape.png":
                    (icons / name).symlink_to(outside)
                self.assertEqual(catalog.flathub_metadata()["org.example.Editor"]["icon"], "")

    def test_main_categories(self):
        for category, expected in {
            "Network": "Internet", "Graphics": "Graphics", "Office": "Productivity",
            "AudioVideo": "Multimedia", "Audio": "Multimedia", "Video": "Multimedia",
            "Game": "Games", "Utility": "Utilities", "Development": "Utilities",
            "Education": "Productivity", "Science": "Productivity",
        }.items():
            self.assertEqual(catalog.main_category([category]), expected)
        self.assertEqual(catalog.main_category([]), "Utilities")
        self.assertEqual(catalog.main_category(["Utility", "Graphics"]), "Graphics")


if __name__ == "__main__":
    unittest.main()
