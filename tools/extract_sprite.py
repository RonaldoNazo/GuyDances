#!/usr/bin/env python3
"""Turn guydances.mp4 into the tiny embedded sprite the app plays.

Output: assets/dance.bin
  magic "GDNC" | u16 version | u16 w | u16 h | u16 palette_count
  u32 frame_count | u32 frame_delay_us
  palette: palette_count * RGBA
  per frame: u32 byte_size, then (count u8, palette_index u8) runs
Index 0 is transparent.
"""
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from collections import deque

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIDEO = os.path.join(ROOT, "guydances.mp4")
OUT = os.path.join(ROOT, "assets", "dance.bin")
PALETTE_COLORS = 12  # plus transparent index 0
FRAME_DELAY_US = 60000
PAD = 2
OUTLINE = (10, 8, 14)


def extract_frames(tmp):
    subprocess.run(
        ["ffmpeg", "-v", "error", "-y", "-i", VIDEO, os.path.join(tmp, "f_%04d.png")],
        check=True,
    )
    files = sorted(glob.glob(os.path.join(tmp, "f_*.png")))
    if not files:
        sys.exit("ffmpeg produced no frames")
    return files


def background_mask(rgb):
    a = rgb.astype(np.int16)
    mx = a.max(axis=2)
    blue = (a[:, :, 2] > 110) & ((a[:, :, 2] - a[:, :, 0]) > 35) & ((a[:, :, 2] - a[:, :, 1]) > 35)
    like = (mx < 120) | blue
    return reachable_from_border(like)


def reachable_from_border(mask):
    """Connected components of `mask`, keep the ones touching an edge.
    Run-length union-find, fast enough for 288 frames."""
    h, w = mask.shape
    parent = []

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    runs = []          # (y, x0, x1, id)
    prev = []          # runs of previous row
    for y in range(h):
        row = mask[y]
        if not row.any():
            prev = []
            continue
        d = np.diff(np.concatenate(([0], row.view(np.int8), [0])))
        starts = np.flatnonzero(d == 1)
        ends = np.flatnonzero(d == -1)
        cur = []
        for x0, x1 in zip(starts, ends):
            rid = len(parent)
            parent.append(rid)
            cur.append((x0, x1, rid))
            runs.append((y, x0, x1, rid))
        # union with overlapping runs one row above
        for x0, x1, rid in cur:
            for px0, px1, prid in prev:
                if px0 < x1 and x0 < px1:
                    union(rid, prid)
        prev = cur

    border_roots = set()
    for y, x0, x1, rid in runs:
        if y == 0 or y == h - 1 or x0 == 0 or x1 == w:
            border_roots.add(find(rid))

    out = np.zeros((h, w), bool)
    for y, x0, x1, rid in runs:
        if find(rid) in border_roots:
            out[y, x0:x1] = True
    return out


def build_palette(frames, masks, files):
    sample = []
    for idx in range(0, len(files), 8):
        rgb = frames[idx]
        m = ~masks[idx]
        sample.append(rgb[m][::5])
    sample = np.concatenate(sample, axis=0)
    rng = np.random.default_rng(7)
    if len(sample) > 300_000:
        sample = sample[rng.choice(len(sample), 300_000, replace=False)]
    strip = Image.fromarray(sample.reshape(1, -1, 3).astype(np.uint8))
    pal_img = strip.quantize(colors=PALETTE_COLORS, method=Image.MEDIANCUT)
    pal = pal_img.getpalette()[: PALETTE_COLORS * 3]
    colors = [tuple(pal[i * 3 : i * 3 + 3]) for i in range(PALETTE_COLORS)]
    return colors


def build_lut(colors):
    # 5 bits per channel LUT -> nearest palette index
    grid = np.stack(np.meshgrid(np.arange(32), np.arange(32), np.arange(32), indexing="ij"), -1)
    grid = (grid * 255 // 31).reshape(-1, 3).astype(np.int32)
    pal = np.array(colors, dtype=np.int32)
    d = ((grid[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
    return (d.argmin(axis=1) + 2).astype(np.uint8)  # 0 transparent, 1 outline


def quantize(crop, lut):
    c = crop.astype(np.int32)
    key = (c[:, :, 0] >> 3) * 1024 + (c[:, :, 1] >> 3) * 32 + (c[:, :, 2] >> 3)
    return lut[key]


def encode_frame(indices):
    flat = indices.reshape(-1)
    runs = []
    n = len(flat)
    i = 0
    while i < n:
        v = flat[i]
        j = i + 1
        while j < n and flat[j] == v and j - i < 255:
            j += 1
        runs.append((j - i, int(v)))
        i = j
    return b"".join(struct.pack("BB", c, v) for c, v in runs)


def main():
    tmp = tempfile.mkdtemp(prefix="guyframes_")
    try:
        files = extract_frames(tmp)
        frames = []
        masks = []
        for f in files:
            rgb = np.asarray(Image.open(f).convert("RGB"))
            frames.append(rgb)
            masks.append(background_mask(rgb))

        ys, xs = np.where(~np.stack(masks).all(axis=0))
        y0, y1 = max(0, ys.min() - PAD), min(frames[0].shape[0], ys.max() + 1 + PAD)
        x0, x1 = max(0, xs.min() - PAD), min(frames[0].shape[1], xs.max() + 1 + PAD)
        w, h = x1 - x0, y1 - y0
        print(f"{len(files)} frames, crop {w}x{h} at ({x0},{y0})")

        colors = build_palette(frames, masks, files)
        lut = build_lut(colors)

        payload = []
        for rgb, mask in zip(frames, masks):
            crop = rgb[y0:y1, x0:x1]
            idx = quantize(crop, lut)
            m = mask[y0:y1, x0:x1]
            idx = idx.copy()
            idx[m] = 0
            fg = ~m
            dil = fg.copy()
            dil[1:, :] |= fg[:-1, :]
            dil[:-1, :] |= fg[1:, :]
            dil[:, 1:] |= fg[:, :-1]
            dil[:, :-1] |= fg[:, 1:]
            idx[dil & ~fg] = 1
            payload.append(encode_frame(idx))

        with open(OUT, "wb") as fh:
            fh.write(b"GDNC")
            fh.write(struct.pack("<HHHHII", 1, w, h, len(colors) + 2, len(payload), FRAME_DELAY_US))
            fh.write(b"\x00\x00\x00\x00")  # transparent
            fh.write(bytes(OUTLINE) + b"\xff")
            for c in colors:
                fh.write(bytes(c) + b"\xff")
            for data in payload:
                fh.write(struct.pack("<I", len(data)))
                fh.write(data)

        total = os.path.getsize(OUT)
        print(f"palette {len(colors)}+transparent, {total/1024:.0f} KiB, {total/len(payload):.0f} B/frame")

        # preview: four frames over a checkerboard
        sheet = []
        for n in (0, len(frames) // 3, 2 * len(frames) // 3, len(frames) - 1):
            crop = frames[n][y0:y1, x0:x1]
            m = masks[n][y0:y1, x0:x1]
            idx = quantize(crop, lut).copy()
            idx[m] = 0
            rgba = np.zeros((h, w, 4), np.uint8)
            pal = [(0, 0, 0, 0), tuple(OUTLINE) + (255,)] + [tuple(c) + (255,) for c in colors]
            for i, c in enumerate(pal):
                rgba[idx == i] = c
            im = Image.fromarray(rgba, "RGBA")
            chk = Image.new("RGBA", (w, h))
            px = chk.load()
            for yy in range(h):
                for xx in range(w):
                    v = 200 if ((xx // 8 + yy // 8) % 2 == 0) else 235
                    px[xx, yy] = (v, v, v, 255)
            sheet.append(Image.alpha_composite(chk, im))
        out = Image.new("RGBA", (w * 4, h), (30, 30, 30, 255))
        for i, im in enumerate(sheet):
            out.paste(im, (i * w, 0), im)
        out.resize((w * 8, h * 2), Image.NEAREST).save(os.path.join(ROOT, "assets", "preview.png"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
