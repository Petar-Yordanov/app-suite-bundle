"""Offline tests for developer SDK discovery and installer publication."""
import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import dev
import publish_release as release


class DiscoveryTests(unittest.TestCase):
    def test_sdk_override_must_be_compatible(self):
        with patch.object(dev, "compatible", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "Incompatible"):
                dev.discover_qt("ImageViewer", "6.8.3", (), Path("/bad"))

    def test_environment_sdk_first(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(dev.os.environ, {"QT_ROOT_DIR": tmp}), patch.object(dev, "compatible", return_value=True):
            self.assertEqual(dev.discover_qt("ImageViewer", "6.8.3", ()), Path(tmp).resolve())

    def test_sdk_version_and_modules(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            lib, plugins, qml = root / "lib", root / "plugins", root / "qml"
            for module in ["Core", "Gui", "Widgets", "Qml", "Quick", "QuickControls2", "Network", "Concurrent", "Svg"]:
                p = lib / "cmake" / f"Qt6{module}" / f"Qt6{module}Config.cmake"
                p.parent.mkdir(parents=True)
                p.touch()
            (qml / "QtQuick/Controls").mkdir(parents=True)
            image = plugins / "imageformats"
            image.mkdir(parents=True)
            (image / "libqwebp.so").touch()
            metadata = {"QT_VERSION": "6.8.3", "QT_INSTALL_LIBS": str(lib), "QT_INSTALL_PLUGINS": str(plugins), "QT_INSTALL_QML": str(qml)}
            with patch.object(dev, "HOST", "Linux"), patch.object(dev, "query_qt", return_value=metadata):
                self.assertFalse(dev.compatible(root, "ImageViewer", "6.8.3"))
                (image / "libqtiff.so").touch()
                self.assertTrue(dev.compatible(root, "ImageViewer", "6.8.3"))
                self.assertFalse(dev.compatible(root, "WebBrowser", "6.11.3"))

    def test_offline_missing_sdk(self):
        with self.assertRaisesRegex(RuntimeError, "No compatible"):
            dev.acquire_qt("WebBrowser", "6.11.3", "linux", "linux_gcc_64", (), False)


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "ImageViewer.exe"
        self.path.write_bytes(b"new installer")
        self.items = {"ImageViewer.exe": {"id": 1, "name": "ImageViewer.exe", "digest": "old", "label": "old version"},
                      "Other.exe": {"id": 2, "name": "Other.exe", "digest": "other"}}
        self.fail_promotion = False
        self.bad_upload = False
        for key, fn in [("assets", self.assets), ("rename", self.rename), ("delete", self.delete), ("gh", self.gh)]:
            p = patch.object(release, key, side_effect=fn)
            p.start()
            self.addCleanup(p.stop)

    def assets(self, *args):
        return copy.deepcopy(self.items)

    def rename(self, repo, asset, name, label):
        if self.fail_promotion and asset["id"] == 3:
            raise RuntimeError("promotion failed")
        old = next(k for k, v in self.items.items() if v["id"] == asset["id"])
        updated = self.items.pop(old)
        updated.update(name=name, label=label)
        self.items[name] = updated
        return copy.deepcopy(updated)

    def delete(self, repo, asset):
        self.items.pop(asset["name"])

    def gh(self, *args):
        self.assertEqual(args[:2], ("release", "upload"))
        file = Path(args[3])
        self.items[file.name] = {"id": 3, "name": file.name, "state": "uploaded", "size": file.stat().st_size, "digest": "bad" if self.bad_upload else release.digest(file)}

    def publish(self):
        release.publish("owner/repo", 42, self.path, "abcd123456789", "1-1")

    def test_success_preserves_unrelated_asset(self):
        self.publish()
        self.assertEqual(set(self.items), {"ImageViewer.exe", "Other.exe"})
        self.assertEqual(self.items["ImageViewer.exe"]["digest"], release.digest(self.path))
        self.assertEqual(self.items["Other.exe"]["digest"], "other")

    def test_invalid_upload_preserves_previous(self):
        self.bad_upload = True
        with self.assertRaisesRegex(RuntimeError, "verification failed"):
            self.publish()
        self.assertEqual(self.items["ImageViewer.exe"]["digest"], "old")

    def test_failed_promotion_restores_previous(self):
        self.fail_promotion = True
        with self.assertRaisesRegex(RuntimeError, "promotion failed"):
            self.publish()
        self.assertEqual(self.items["ImageViewer.exe"]["digest"], "old")
        self.assertIn("Other.exe", self.items)


if __name__ == "__main__":
    unittest.main()
