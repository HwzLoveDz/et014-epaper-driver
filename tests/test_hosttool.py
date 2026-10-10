"""Pure host image/protocol tests; no serial port opened."""
import importlib.util
from pathlib import Path
import tempfile, unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('epdtool',ROOT/'tools/epdtool.py')
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)

class ImageTest(unittest.TestCase):
    def test_four_levels_msb(self):
        gray=bytes([0,85,170,255])*(tool.W*tool.H//4)
        self.assertEqual(tool.pack(gray),bytes([0x1b])*tool.BYTES)
    def test_binary_extremes(self):
        for method in ('mono','bayer','gray'):
            self.assertEqual(tool.pack(bytes(tool.W*tool.H),method),bytes(tool.BYTES))
            self.assertEqual(tool.pack(bytes([255])*(tool.W*tool.H),method),bytes([255])*tool.BYTES)
    def test_native_city(self):
        old=(ROOT/'assets/pixel-city-128x296.bin').read_bytes()
        packed=tool.pack(tool.read_gray(ROOT/'assets/pixel-city-128x296.bin'))
        for i in range(tool.W*tool.H):
            self.assertEqual((packed[i//4]>>(6-2*(i%4)))&3,0 if old[i//8]&(0x80>>(i%8)) else 3)
    def test_pgm_binary_whitespace_is_pixel(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'test.pgm';pixels=bytes([10,13,32,35])*(tool.W*tool.H//4)
            path.write_bytes(b'P5\n# header comment\n128 296\n255\n'+pixels)
            self.assertEqual(tool.read_gray(path),pixels)
            path.write_bytes(b'P5\r\n128 296\r\n255\r\n'+pixels)
            self.assertEqual(tool.read_gray(path),pixels)
    def test_wrong_size_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'bad.bin';path.write_bytes(b'\0'*100)
            with self.assertRaises(ValueError):tool.read_gray(path)
        with self.assertRaises(ValueError):tool.pack(b'bad')
    def test_pbm_one_is_black(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'test.pbm';path.write_bytes(b'P4\n128 296\n'+bytes([0x80])*(tool.W*tool.H//8))
            self.assertEqual(tool.read_gray(path)[:8],bytes([0,255,255,255,255,255,255,255]))
    def test_preview_png(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'preview.png';tool.preview(bytes(tool.BYTES),path)
            self.assertTrue(path.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'))

if __name__=='__main__': unittest.main()
