#!python3
import freetype
import zlib
import sys
import re
import math
import argparse
import os
from collections import namedtuple

# Force binary output mode on Windows
if sys.platform == 'win32':
    import msvcrt
    msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)

parser = argparse.ArgumentParser(description="Generate a header file from a font to be used with epdiy.")
parser.add_argument("name", action="store", help="name of the font.")
parser.add_argument("size", type=int, help="font size to use.")
parser.add_argument("fontstack", action="store", nargs='+', help="list of font files, ordered by descending priority.")
parser.add_argument("--compress", dest="compress", action="store_true", help="compress glyph bitmaps.")
args = parser.parse_args()

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "compressed_size", "data_offset", "code_point"])

font_stack = [freetype.Face(f) for f in args.fontstack]
compress = args.compress
size = args.size
font_name = args.name

# inclusive unicode code point intervals
# must not overlap and be in ascending order
intervals = [
    (32, 126),  # Basic Latin
    (160, 255), # Latin-1 Supplement (includes some Polish characters)
    (0x104, 0x107),  # Ą ą Ć ć
    (0x118, 0x119),  # Ę ę
    (0x141, 0x144),  # Ł ł Ń ń
    (0x15A, 0x15B),  # Ś ś
    (0x179, 0x17C),  # Ź ź Ż ż
]

def norm_floor(val):
    return int(math.floor(val / (1 << 6)))

def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))

for face in font_stack:
    face.set_char_size(size << 6, size << 6, 150, 150)

def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i:i + n]

def write_line(text):
    """Write a line of text in UTF-8 encoding."""
    sys.stdout.buffer.write(text.encode('utf-8'))
    sys.stdout.buffer.write(b'\n')

def safe_chr(code_point):
    """Safely convert code point to character, handling special cases."""
    try:
        if code_point == 92:  # backslash
            return '<backslash>'
        return chr(code_point)
    except:
        return f'<{hex(code_point)}>'

total_size = 0
total_packed = 0
all_glyphs = []

def load_glyph(code_point):
    face_index = 0
    while face_index < len(font_stack):
        face = font_stack[face_index]
        glyph_index = face.get_char_index(code_point)
        if glyph_index > 0:
            face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
            return face
        face_index += 1
        print(f"falling back to font {face_index} for {safe_chr(code_point)}.", file=sys.stderr)
    raise ValueError(f"code point {code_point} not found in font stack!")

for i_start, i_end in intervals:
    for code_point in range(i_start, i_end + 1):
        face = load_glyph(code_point)
        bitmap = face.glyph.bitmap
        pixels = []
        px = 0
        for i, v in enumerate(bitmap.buffer):
            y = i / bitmap.width
            x = i % bitmap.width
            if x % 2 == 0:
                px = (v >> 4)
            else:
                px = px | (v & 0xF0)
                pixels.append(px)
                px = 0
            # eol
            if x == bitmap.width - 1 and bitmap.width % 2 > 0:
                pixels.append(px)
                px = 0

        packed = bytes(pixels)
        total_packed += len(packed)
        compressed = packed
        if compress:
            compressed = zlib.compress(packed)

        glyph = GlyphProps(
            width = bitmap.width,
            height = bitmap.rows,
            advance_x = norm_floor(face.glyph.advance.x),
            left = face.glyph.bitmap_left,
            top = face.glyph.bitmap_top,
            compressed_size = len(compressed),
            data_offset = total_size,
            code_point = code_point,
        )
        total_size += len(compressed)
        all_glyphs.append((glyph, compressed))

face = load_glyph(ord('|'))

glyph_data = []
glyph_props = []
for index, glyph in enumerate(all_glyphs):
    props, compressed = glyph
    glyph_data.extend([b for b in compressed])
    glyph_props.append(props)

print("total", total_packed, file=sys.stderr)
print("compressed", total_size, file=sys.stderr)

# Write output directly in UTF-8
write_line("// UTF-8")
write_line("#pragma once")
write_line("#include \"epd_driver.h\"")
write_line(f"const uint8_t {font_name}Bitmaps[{len(glyph_data)}] = {{")
for c in chunks(glyph_data, 16):
    write_line("    " + " ".join(f"0x{b:02X}," for b in c))
write_line("};")

write_line(f"const GFXglyph {font_name}Glyphs[] = {{")
for i, g in enumerate(glyph_props):
    comment = f"// {safe_chr(g.code_point)}"
    write_line("    { " + ", ".join([f"{a}" for a in list(g[:-1])]) + "}," + comment)
write_line("};")

write_line(f"const UnicodeInterval {font_name}Intervals[] = {{")
offset = 0
for i_start, i_end in intervals:
    write_line(f"    {{ 0x{i_start:X}, 0x{i_end:X}, 0x{offset:X} }},")
    offset += i_end - i_start + 1
write_line("};")

write_line(f"const GFXfont {font_name} = {{")
write_line(f"    (uint8_t*){font_name}Bitmaps,")
write_line(f"    (GFXglyph*){font_name}Glyphs,")
write_line(f"    (UnicodeInterval*){font_name}Intervals,")
write_line(f"    {len(intervals)},")
write_line(f"    {1 if compress else 0},")
write_line(f"    {norm_ceil(face.size.height)},")
write_line(f"    {norm_ceil(face.size.ascender)},")
write_line(f"    {norm_floor(face.size.descender)},")
write_line("};")