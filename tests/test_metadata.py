import importlib.util
from pathlib import Path
import re
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
                         list(range(100)))
        self.assertEqual(len(re.findall(r'lv2:symbol "(?:note|cc)\d+" .*?lv2:default -1', text)), 32)
        for pad in range(1, 17):
            self.assertIn(f'<{metadata.URI}:sample{pad:02}> a lv2:Parameter', text)
            for symbol in ("mode", "note", "cc", "gain", "trigger", "active"):
                self.assertIn(f'lv2:symbol "{symbol}{pad:02}"', text)
        self.assertIn("worker:schedule", text)
        self.assertIn("state:interface", text)
        self.assertEqual(len(re.findall(
            r'lv2:symbol "gain\d+" .*?lv2:default 0 ; lv2:minimum -60 ; lv2:maximum 12', text)), 16)
        self.assertNotIn("modgui:gui", text)

    def test_generated_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "16-pads.lv2"
            metadata.generate(root, True)
            self.assertIn("modgui:gui", (root / "sampler.ttl").read_text())
            self.assertIn("modgui:monitoredOutputs", (root / "sampler.ttl").read_text())
            self.assertIn("sampler.so", (root / "manifest.ttl").read_text())
            for name in ("icon.html", "style.css", "script.js"):
                self.assertTrue((root / "modgui" / name).is_file())


if __name__ == "__main__":
    unittest.main()
