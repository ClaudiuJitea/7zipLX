# 7zipLX context menu for GNOME Files (Nautilus 43 and newer).
#
# Installed to /usr/share/nautilus-python/extensions/ and loaded by the
# python3-nautilus package.  Restart Files after installing it:  nautilus -q
#
# Archives get extract/test/open actions; any other selection gets the
# compress actions.  Names follow the same rules as the 7zipLX window.

import os
import re
import shutil
import subprocess
from urllib.parse import unquote, urlparse

from gi.repository import GObject, Nautilus

ARCHIVE_EXTENSIONS = {
    "7z", "zip", "zipx", "rar", "r00", "tar", "gz", "gzip", "tgz", "tpz",
    "bz2", "bzip2", "tbz", "tbz2", "xz", "txz", "zst", "tzst", "lzma",
    "lzma86", "z", "taz", "cab", "iso", "udf", "wim", "swm", "esd", "arj",
    "lzh", "lha", "cpio", "rpm", "deb", "udeb", "ar", "jar", "xpi", "apk",
    "xar", "pkg", "dmg", "squashfs", "cramfs", "ova", "001", "z01",
}

_VOLUME = re.compile(r"(\.part\d+)?\.(\d{3}|r\d\d|z\d\d)$", re.IGNORECASE)
_PART_RAR = re.compile(r"\.part\d+(?=\.rar$)", re.IGNORECASE)


def _program():
    return shutil.which("7ziplx") or "7ziplx"


def _is_archive(path):
    name = os.path.basename(path)
    if "." not in name.lstrip("."):
        return False
    return name.rsplit(".", 1)[1].lower() in ARCHIVE_EXTENSIONS


def _folder_name(path):
    name = os.path.basename(path)
    name = _PART_RAR.sub("", name)
    name = _VOLUME.sub("", name)
    for _ in range(4):
        dot = name.rfind(".")
        if dot <= 0 or name[dot + 1:].lower() not in ARCHIVE_EXTENSIONS:
            break
        name = name[:dot]
    return name or "extracted"


def _quick_name(paths):
    if len(paths) == 1:
        path = paths[0]
        name = os.path.basename(path.rstrip("/"))
        if os.path.isdir(path):
            return name or "archive"
        stem = name.rsplit(".", 1)[0] if "." in name.lstrip(".") else name
        return stem or name or "archive"
    return os.path.basename(os.path.dirname(paths[0])) or "archive"


def _short(text, limit=40):
    return text if len(text) <= limit else text[: limit // 2 - 1] + "…" + text[-(limit // 2 - 1):]


def _local_paths(files):
    paths = []
    for item in files:
        uri = item.get_uri()
        if not uri.startswith("file://"):
            return []
        paths.append(unquote(urlparse(uri).path))
    return paths


def _run(*args):
    try:
        subprocess.Popen([_program(), *args], start_new_session=True,
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    except OSError:
        pass


class SevenZipLXMenu(GObject.GObject, Nautilus.MenuProvider):
    def _item(self, menu, key, label, tip, args, paths):
        item = Nautilus.MenuItem(name=f"SevenZipLX::{key}", label=label, tip=tip)
        item.connect("activate", lambda _item: _run(*args, *paths))
        menu.append_item(item)

    def get_file_items(self, *args):
        files = args[-1]  # (files) on Nautilus 4, (window, files) on 3
        paths = _local_paths(files)
        if not paths:
            return []

        root = Nautilus.MenuItem(name="SevenZipLX::Menu", label="7-Zip",
                                 tip="7zipLX archive actions")
        menu = Nautilus.Menu()
        root.set_submenu(menu)

        archives = [p for p in paths if os.path.isfile(p) and _is_archive(p)]
        if archives and len(archives) == len(paths):
            if len(archives) == 1:
                self._item(menu, "Open", "Open in 7zipLX", "Browse the archive", [], archives)
            self._item(menu, "Extract", "Extract Files…", "Choose where and how to extract",
                       ["--extract"], archives)
            self._item(menu, "ExtractHere", "Extract Here",
                       "Extract into this folder", ["--extract-here"], archives)
            if len(archives) == 1:
                label = f"Extract to \"{_short(_folder_name(archives[0]))}/\""
            else:
                label = "Extract Each to Its Own Folder"
            self._item(menu, "ExtractToFolder", label, "Extract into a new folder named after the archive",
                       ["--extract-to-folder"], archives)
            self._item(menu, "Test", "Test Archive", "Check the archive for errors",
                       ["--test"], archives)

        base = _short(_quick_name(paths))
        self._item(menu, "Add", "Add to Archive…", "Choose the format and options",
                   ["--add"], paths)
        self._item(menu, "Compress7z", f"Add to \"{base}.7z\"", "Create a 7z archive",
                   ["--compress", "--format", "7z"], paths)
        self._item(menu, "CompressZip", f"Add to \"{base}.zip\"", "Create a zip archive",
                   ["--compress", "--format", "zip"], paths)
        return [root]

    def get_background_items(self, *args):
        return []
