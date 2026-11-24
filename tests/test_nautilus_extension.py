"""Checks the Nautilus extension's menus and commands without Nautilus.

Run: python3 -I tests/test_nautilus_extension.py packaging/nautilus/7ziplx.py
"""
import importlib.util
import os
import sys
import tempfile
import types
import unittest
from urllib.parse import quote


class FakeMenuItem:
    def __init__(self, name, label, tip=""):
        self.name, self.label, self.tip = name, label, tip
        self.submenu = None
        self.callbacks = []

    def set_submenu(self, menu):
        self.submenu = menu

    def connect(self, signal, callback):
        assert signal == "activate"
        self.callbacks.append(callback)

    def activate(self):
        for callback in self.callbacks:
            callback(self)


class FakeMenu:
    def __init__(self):
        self.items = []

    def append_item(self, item):
        self.items.append(item)


class FakeFile:
    def __init__(self, path):
        self.path = path

    def get_uri(self):
        return "file://" + quote(self.path)


def load_extension(path):
    class FakeGObject:
        pass

    class FakeMenuProvider:
        pass

    nautilus = types.SimpleNamespace(MenuItem=FakeMenuItem, Menu=FakeMenu, MenuProvider=FakeMenuProvider)
    gobject = types.SimpleNamespace(GObject=FakeGObject)
    repository = types.ModuleType("gi.repository")
    repository.Nautilus = nautilus
    repository.GObject = gobject
    gi = types.ModuleType("gi")
    gi.repository = repository
    sys.modules["gi"] = gi
    sys.modules["gi.repository"] = repository
    spec = importlib.util.spec_from_file_location("ext7ziplx", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ExtensionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ext = load_extension(sys.argv[1] if len(sys.argv) > 1 else "packaging/nautilus/7ziplx.py")

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = os.path.join(self.dir.name, "Downloads")
        os.mkdir(self.root)
        self.launched = []
        self.ext._run = lambda *args: self.launched.append(list(args))

    def tearDown(self):
        self.dir.cleanup()

    def touch(self, name):
        path = os.path.join(self.root, name)
        if name.endswith("/"):
            os.makedirs(path, exist_ok=True)
            return path.rstrip("/")
        with open(path, "w") as handle:
            handle.write("x")
        return path

    def menu_for(self, *paths):
        provider = self.ext.SevenZipLXMenu()
        items = provider.get_file_items([FakeFile(p) for p in paths])
        if not items:
            return None
        self.assertEqual(items[0].label, "7-Zip")
        return {item.label: item for item in items[0].submenu.items}

    def test_archive_selection(self):
        deb = self.touch("7ziplx_0.5.0-1_amd64.deb")
        menu = self.menu_for(deb)
        self.assertEqual(list(menu), [
            "Open in 7zipLX", "Extract Files…", "Extract Here",
            "Extract to \"7ziplx_0.5.0-1_amd64/\"", "Test Archive",
            "Add to Archive…", "Add to \"7ziplx_0.5.0-1_amd64.7z\"",
            "Add to \"7ziplx_0.5.0-1_amd64.zip\""])
        menu["Extract Here"].activate()
        menu["Extract to \"7ziplx_0.5.0-1_amd64/\""].activate()
        menu["Open in 7zipLX"].activate()
        self.assertEqual(self.launched, [["--extract-here", deb], ["--extract-to-folder", deb], [deb]])

    def test_several_archives(self):
        a = self.touch("a.7z")
        b = self.touch("b.tar.gz")
        menu = self.menu_for(a, b)
        self.assertNotIn("Open in 7zipLX", menu)
        self.assertIn("Extract Each to Its Own Folder", menu)
        self.assertIn("Add to \"Downloads.7z\"", menu)
        menu["Test Archive"].activate()
        self.assertEqual(self.launched, [["--test", a, b]])

    def test_plain_files_only_compress(self):
        a = self.touch("report.txt")
        folder = self.touch("photos/")
        menu = self.menu_for(a, folder)
        self.assertEqual(list(menu), ["Add to Archive…", "Add to \"Downloads.7z\"", "Add to \"Downloads.zip\""])
        menu["Add to \"Downloads.zip\""].activate()
        self.assertEqual(self.launched, [["--compress", "--format", "zip", a, folder]])

    def test_mixed_selection_offers_compress_only(self):
        menu = self.menu_for(self.touch("a.zip"), self.touch("notes.txt"))
        self.assertNotIn("Extract Here", menu)
        self.assertIn("Add to Archive…", menu)

    def test_names(self):
        ext = self.ext
        self.assertEqual(ext._folder_name("/x/backup.tar.gz"), "backup")
        self.assertEqual(ext._folder_name("/x/disk.7z.001"), "disk")
        self.assertEqual(ext._folder_name("/x/movie.part1.rar"), "movie")
        self.assertEqual(ext._folder_name("/x/report.v2.zip"), "report.v2")
        self.assertEqual(ext._quick_name([self.touch("data.tar.gz")]), "data.tar")
        self.assertEqual(ext._quick_name([self.touch(".profile")]), ".profile")
        self.assertEqual(ext._quick_name([self.touch("lib.d/")]), "lib.d")
        self.assertFalse(ext._is_archive("/x/setup.exe"))
        self.assertTrue(ext._is_archive("/x/APP.JAR"))

    def test_remote_files_get_no_menu(self):
        class Remote(FakeFile):
            def get_uri(self):
                return "sftp://host/file.zip"
        provider = self.ext.SevenZipLXMenu()
        self.assertEqual(provider.get_file_items([Remote("x")]), [])

    def test_spaces_and_unicode_paths(self):
        path = self.touch("Fotografii vară 2026.zip")
        menu = self.menu_for(path)
        menu["Extract Here"].activate()
        self.assertEqual(self.launched, [["--extract-here", path]])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=1)
