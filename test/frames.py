#!/usr/bin/env python3
"""Raw 375 x 320 BGRX frames from test/sim (the view) as PNGs, with the glass's 80 px rounded
corners drawn in, 2x. Usage: frames.py DIR  (writes DIR/*.png next to DIR/*.raw)"""
import pathlib, sys
from PIL import Image, ImageDraw

d = pathlib.Path(sys.argv[1])
for raw in sorted(d.glob('*.raw')):
    im = Image.frombytes('RGBX', (375, 320), raw.read_bytes(), 'raw', 'BGRX').convert('RGB')
    mask = Image.new('L', im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, 374, 319], radius=80, fill=255)
    glass = Image.new('RGB', im.size, (40, 40, 40))
    glass.paste(im, (0, 0), mask)
    glass.resize((750, 640), Image.Resampling.LANCZOS).save(raw.with_suffix('.png'))
