"""PNG などの画像と growfill_cli の raw 形式 (IPRF) を相互変換する。
  python3 rgba_convert.py to-raw in.png out.iprf
  python3 rgba_convert.py to-png in.iprf out.png
"""
import struct, sys
import numpy as np
from PIL import Image

def to_raw(src, dst):
    a = np.asarray(Image.open(src).convert("RGBA"), dtype=np.float32) / 255.0
    h, w, _ = a.shape
    with open(dst, "wb") as f:
        f.write(b"IPRF" + struct.pack("<ii", w, h) + a.astype("<f4").tobytes())

def to_png(src, dst):
    with open(src, "rb") as f:
        assert f.read(4) == b"IPRF"
        w, h = struct.unpack("<ii", f.read(8))
        a = np.frombuffer(f.read(), dtype="<f4").reshape(h, w, 4)
    Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(dst)

if __name__ == "__main__":
    {"to-raw": to_raw, "to-png": to_png}[sys.argv[1]](sys.argv[2], sys.argv[3])
