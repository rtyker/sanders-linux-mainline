#!/usr/bin/env python3
"""Zero-dependency PNG icon generator for the sanders Weston panel (BF 2026-09-10).
Draws 48x48 pixel-art icons (gear, note, terminal) using only zlib+struct."""
import zlib
import struct
import sys

W = H = 48


def png(path, px):
    """px[y][x] = (r, g, b, a)"""
    raw = b""
    for y in range(H):
        raw += b"\x00" + b"".join(bytes(px[y][x]) for x in range(W))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        c += struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        return c
    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(raw, 9))
    out += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def blank():
    return [[(0, 0, 0, 0) for _ in range(W)] for _ in range(H)]


def fill(px, x0, y0, x1, y1, col):
    for y in range(max(0, y0), min(H, y1 + 1)):
        for x in range(max(0, x0), min(W, x1 + 1)):
            px[y][x] = col


def circle(px, cx, cy, r, col):
    for y in range(H):
        for x in range(W):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px[y][x] = col


def ring(px, cx, cy, r, t, col):
    for y in range(H):
        for x in range(W):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if (r - t) ** 2 <= d2 <= r * r:
                px[y][x] = col


WHITE = (235, 238, 242, 255)
BLUE = (33, 150, 243, 255)
GREEN = (76, 175, 80, 255)
DARK = (16, 18, 22, 255)
AMBER = (255, 179, 0, 255)


def icon_gear(path):
    px = blank()
    fill(px, 0, 0, 47, 47, DARK)  # tile background
    cx = cy = 23
    ring(px, cx, cy, 12, 5, WHITE)
    for ang in range(0, 360, 45):
        import math
        a = math.radians(ang)
        x0 = int(cx + 10 * math.cos(a)) - 2
        y0 = int(cy + 10 * math.sin(a)) - 2
        fill(px, x0, y0, x0 + 4, y0 + 4, WHITE)  # teeth
    fill(px, cx - 3, cy - 3, cx + 3, cy + 3, (0, 0, 0, 0))  # hole
    # hole must stay transparent over dark bg: redraw dark
    fill(px, cx - 3, cy - 3, cx + 3, cy + 3, DARK)
    circle(px, cx, cy, 3, BLUE)
    png(path, px)


def icon_note(path):
    px = blank()
    fill(px, 0, 0, 47, 47, DARK)
    fill(px, 30, 8, 33, 32, WHITE)   # stem
    fill(px, 30, 8, 40, 11, WHITE)   # flag top
    circle(px, 26, 34, 7, BLUE)      # head
    fill(px, 26, 30, 33, 32, BLUE)   # head join
    png(path, px)


def icon_terminal(path):
    px = blank()
    fill(px, 0, 0, 47, 47, DARK)
    fill(px, 4, 6, 43, 41, (30, 34, 40, 255))
    fill(px, 4, 6, 43, 10, (60, 66, 76, 255))
    # prompt ">_"
    for i in range(8):
        fill(px, 10 + i, 18 + i, 11 + i, 18 + i, GREEN)
        fill(px, 10 + i, 32 - i, 11 + i, 32 - i, GREEN)
    fill(px, 24, 30, 36, 32, WHITE)
    fill(px, 30, 8, 33, 32, WHITE) if False else None
    png(path, px)


def icon_wifi(path):
    px = blank()
    fill(px, 0, 0, 47, 47, DARK)
    ring(px, 23, 44, 34, 3, WHITE)   # outer arc
    ring(px, 23, 44, 24, 3, WHITE)   # mid arc
    circle(px, 23, 36, 4, BLUE)      # dot
    # keep only top half-ish of arcs (wifi opens upward)
    for y in range(36, 48):
        for x in range(W):
            px[y][x] = (0, 0, 0, 0)
    for y in range(36, 48):
        for x in range(W):
            px[y][x] = (0, 0, 0, 0)
    # redraw tile bg below y=36
    for y in range(36, 48):
        for x in range(W):
            px[y][x] = DARK
    # dot must stay visible
    circle(px, 23, 34, 4, BLUE)
    png(path, px)


def icon_bt(path):
    px = blank()
    fill(px, 0, 0, 47, 47, DARK)
    fill(px, 22, 6, 25, 41, WHITE)           # vertical
    # rune diagonals (pixel line art)
    for i in range(12):
        fill(px, 24 + i * 0, 20 + i, 24 + i * 0, 20 + i, WHITE) if False else None
    for i in range(12):
        y = 8 + i
        fill(px, 24 + (12 - i) // 2 - (12 - i) % 2, y, 25 + (12 - i) // 2, y, WHITE)
    for i in range(12):
        y = 20 + i
        fill(px, 24 + (12 - i) // 2 - (12 - i) % 2, y, 25 + (12 - i) // 2, y, WHITE)
    fill(px, 24, 8, 25, 20, WHITE)
    fill(px, 24, 26, 25, 38, WHITE)
    png(path, px)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    icon_gear(f"{out}/quicksettings.png")
    icon_note(f"{out}/player.png")
    icon_terminal(f"{out}/terminal.png")
    icon_wifi(f"{out}/wifi.png")
    icon_bt(f"{out}/bt.png")
    print("icons written to", out)
