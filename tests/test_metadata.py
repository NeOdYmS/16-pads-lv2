import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "metadata", Path(__file__).resolve().parents[1] / "tools" / "metadata.py"
)
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)


class MetadataTests(unittest.TestCase):
    def test_ports_and_sample_properties(self):
        text = metadata.metadata()
        self.assertEqual(list(map(int, re.findall(r"lv2:index (\d+)", text))),
                         list(range(70)))
        self.assertNotRegex(text, r'lv2:symbol "(?:note|cc)\d+"')
        for pad in range(1, 17):
            self.assertIn(f'<{metadata.URI}:sample{pad:02}> a lv2:Parameter', text)
            for symbol in ("mode", "gain", "trigger", "active"):
                self.assertIn(f'lv2:symbol "{symbol}{pad:02}"', text)
        self.assertIn("worker:schedule", text)
        self.assertIn("state:interface", text)
        self.assertEqual(len(re.findall(
            r'lv2:symbol "gain\d+" .*?lv2:default 0 ; lv2:minimum -60 ; lv2:maximum 12', text)), 16)
        self.assertNotIn("modgui:gui", text)
        self.assertIn('foaf:name "NeOdYmS"', text)
        self.assertIn("doap:homepage", text)
        self.assertIn("lv2:microVersion 13", text)
        self.assertIn("lv2:hardRTCapable", text)
        # MOD-UI truncates control labels to 16 characters.
        for name in re.findall(r'lv2:ControlPort ;[^]]*?lv2:name "([^"]*)"', text):
            self.assertLessEqual(len(name), 16, name)
        self.assertIn('lv2:name "Pad 01 Trigger"', text)
        self.assertIn("doap:license <http://opensource.org/licenses/MIT>", text)
        self.assertEqual(text.count('mod:fileTypes "audiotrack"'), 16)
        for name in ("level_l", "level_r"):
            self.assertIn(f'lv2:symbol "{name}"', text)
            self.assertIn(f'lv2:symbol "{name}"', metadata.gui_metadata())

    def test_generated_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "16-pads.lv2"
            metadata.generate(root, True)
            gui = (root / "modgui.ttl").read_text()
            self.assertIn("modgui:gui", gui)
            self.assertIn("modgui:monitoredOutputs", gui)
            self.assertIn("<modgui.ttl>", (root / "manifest.ttl").read_text())
            self.assertIn("sampler.so", (root / "manifest.ttl").read_text())
            for name in ("icon.html", "style.css", "script.js", "screenshot.png", "thumbnail.png"):
                self.assertTrue((root / "modgui" / name).is_file())
            for name in ("screenshot", "thumbnail"):
                self.assertIn(f"modgui:{name} <modgui/{name}.png>", gui)
                data = (root / "modgui" / f"{name}.png").read_bytes()
                self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
                width, height = struct.unpack(">II", data[16:24])
                self.assertGreater(width, 0)
                self.assertGreater(height, 0)
                if name == "thumbnail":
                    self.assertLessEqual(width, 256)
                    self.assertLessEqual(height, 64)
            metadata.generate(root, False)
            self.assertNotIn("<modgui.ttl>", (root / "manifest.ttl").read_text())
            self.assertFalse((root / "modgui.ttl").exists())


if __name__ == "__main__":
    unittest.main()
