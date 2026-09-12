"""Generate a smooth snail icon for SnailNet.

Drawn at 4x and downsampled with LANCZOS, which anti-aliases the edges. The
earlier version scaled a 16x16 grid with NEAREST, so every source pixel became
a visible block at display size.
"""

# pylint: disable=import-error

import io
import math
import struct

from PIL import Image, ImageDraw

SUPERSAMPLE = 4
CANVAS = 256 * SUPERSAMPLE


def s(value):
    """Scale a design-space coordinate into the supersampled canvas."""
    return int(value * SUPERSAMPLE)


OUTLINE = (38, 26, 22, 255)
SHELL_MID = (198, 122, 44, 255)
SHELL_LIGHT = (245, 198, 108, 255)
SHELL_DARK = (150, 88, 28, 255)
BODY = (140, 196, 118, 255)
BODY_DARK = (104, 152, 86, 255)
EYE = (30, 22, 20, 255)


def draw_snail():
    """Render the snail at supersampled resolution."""
    image = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    stroke = s(9)

    # --- Eyestalks, drawn first so the head overlaps their base ------------
    for stalk_x, eye_x in ((s(168), s(166)), (s(206), s(208))):
        draw.line(
            [(stalk_x, s(96)), (stalk_x - s(4), s(42))],
            fill=OUTLINE,
            width=stroke,
        )
        draw.ellipse(
            [eye_x - s(15), s(26), eye_x + s(15), s(56)],
            fill=BODY,
            outline=OUTLINE,
            width=stroke,
        )
        draw.ellipse(
            [eye_x - s(6), s(35), eye_x + s(6), s(47)],
            fill=EYE,
        )

    # --- Head, drawn before the foot so the foot hides its lower edge -----
    draw.ellipse(
        [s(150), s(84), s(238), s(196)],
        fill=BODY,
        outline=OUTLINE,
        width=stroke,
    )

    # --- Foot ---------------------------------------------------------------
    draw.rounded_rectangle(
        [s(18), s(176), s(240), s(228)],
        radius=s(26),
        fill=BODY,
        outline=OUTLINE,
        width=stroke,
    )

    # --- Shell --------------------------------------------------------------
    shell_box = [s(16), s(38), s(178), s(200)]
    draw.ellipse(shell_box, fill=SHELL_MID, outline=OUTLINE, width=stroke)

    centre_x = (shell_box[0] + shell_box[2]) / 2
    centre_y = (shell_box[1] + shell_box[3]) / 2

    # Offset inner disc leaves a crescent of the mid tone on the lower right,
    # which reads as a lit sphere rather than a flat patch.
    draw.ellipse(
        [s(24), s(46), s(154), s(176)],
        fill=SHELL_LIGHT,
    )

    # Archimedean spiral, thick enough to read at small sizes.
    spiral = []
    turns = 2.4
    steps = 600
    radius_max = s(62)

    for step in range(steps):
        progress = step / steps
        angle = progress * turns * 2 * math.pi
        radius = radius_max * (1 - progress * 0.92)
        spiral.append(
            (
                centre_x + radius * math.cos(angle),
                centre_y + radius * math.sin(angle),
            )
        )

    draw.line(spiral, fill=SHELL_DARK, width=s(11), joint="curve")

    return image


def write_ico(source, path, sizes):
    """Assemble an ICO by hand so each entry is a clean LANCZOS downsample."""
    entries = []

    for size in sizes:
        scaled = source.resize((size, size), Image.LANCZOS)
        buffer = io.BytesIO()
        scaled.save(buffer, format="PNG")
        entries.append((size, buffer.getvalue()))

    header = struct.pack("<HHH", 0, 1, len(entries))
    offset = len(header) + 16 * len(entries)
    directory = b""

    for size, payload in entries:
        byte_size = 0 if size >= 256 else size
        directory += struct.pack(
            "<BBBBHHII", byte_size, byte_size, 0, 0, 1, 32, len(payload), offset
        )
        offset += len(payload)

    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(directory)
        for _, payload in entries:
            handle.write(payload)


def main():
    """Write the preview PNG and the multi-resolution ICO."""
    large = draw_snail()
    master = large.resize((256, 256), Image.LANCZOS)

    master.save("snail_preview.png")

    write_ico(
        master,
        "snail.ico",
        [16, 24, 32, 48, 64, 80, 96, 128, 256],
    )

    print("wrote snail_preview.png, snail.ico")


if __name__ == "__main__":
    main()
