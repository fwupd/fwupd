#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
# Copyright (C) 2017 Peter Jones <pjones@redhat.com>
# Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=wrong-import-position,too-many-locals,unused-argument,too-many-statements
# pylint: disable=invalid-name,too-many-instance-attributes,missing-module-docstring
# pylint: disable=missing-function-docstring,missing-class-docstring,too-few-public-methods

import os
import sys
import argparse
import tarfile
import math
import io
import struct

from typing import Dict, Optional, Any

import cairo
import gi

gi.require_version("Pango", "1.0")
gi.require_version("PangoCairo", "1.0")
from gi.repository import Pango, PangoCairo


def languages(podir: str):
    for x in open(os.path.join(podir, "LINGUAS"), "r").readlines():
        yield x.strip()
    yield "en"


class PotFile:
    def __init__(self, fn=None):
        self.msgs: Dict[str, str] = {}
        if fn:
            self.parse(fn)

    def parse(self, fn: str) -> None:
        with open(fn, "r") as f:
            lang_en: Optional[str] = None
            for line in f.read().split("\n"):
                if not line:
                    continue
                if line[0] == "#":
                    continue
                try:
                    key, value = line.split(" ", maxsplit=1)
                except ValueError:
                    continue
                if key == "msgid":
                    lang_en = value[1:-1]
                    continue
                if key == "msgstr" and lang_en:
                    self.msgs[lang_en] = value[1:-1]
                    lang_en = None
                    continue


def _cairo_surface_write_to_bmp(img: cairo.ImageSurface) -> bytes:

    data = bytes(img.get_data())
    return (
        b"BM"
        + struct.pack(
            "<ihhiiiihhiiiiii",
            54 + len(data),  # size of BMP file
            0,  # unused
            0,  # unused
            54,  # pixel array offset
            40,  # DIB header
            img.get_width(),  # width
            img.get_height(),  # height
            1,  # planes
            32,  # BPP
            0,  # no compression
            len(data),  # size of the raw bitmap data
            2835,  # 72DPI H
            2835,  # 72DPI V
            0,  # palette
            0,  # all colors are important
        )
        + data
    )


def main(args) -> int:

    # open output archive
    with tarfile.open(args.out, "w:xz") as tar:

        for lang in languages(args.podir):
            # these are the 1.6:1 of some common(ish) screen widths
            if lang == "en":
                label_translated: str = args.label
            else:
                potfile = PotFile(os.path.join(args.podir, "{}.po".format(lang)))
                try:
                    label_translated = potfile.msgs[args.label]
                except KeyError:
                    continue
                if label_translated == args.label:
                    continue
            for width, height in (
                (640, 480),
                (800, 600),
                (1024, 768),
                (1280, 720),
                (1280, 800),
                (1366, 768),
                (1536, 864),
                (1600, 900),
                (1920, 1080),
                (1920, 1200),
                (2160, 1350),
                (2560, 1440),
                (3840, 2160),
                (5120, 2880),
                (5688, 3200),
                (7680, 4320),
            ):

                # generate PangoLanguage
                font_desc = "Sans %.2fpx" % (height / 32,)
                fd = Pango.FontDescription(font_desc)
                font_option = cairo.FontOptions()
                font_option.set_antialias(cairo.ANTIALIAS_SUBPIXEL)
                l = Pango.Language.from_string(lang)

                # create surface
                img = cairo.ImageSurface(cairo.FORMAT_RGB24, 1, 1)
                cctx = cairo.Context(img)
                layout = PangoCairo.create_layout(cctx)
                pctx = layout.get_context()
                pctx.set_font_description(fd)
                pctx.set_language(l)
                fs = pctx.load_fontset(fd, l)
                PangoCairo.context_set_font_options(pctx, font_option)

                attrs = Pango.AttrList()
                length = len(bytes(label_translated, "utf8"))
                items = Pango.itemize(pctx, label_translated, 0, length, attrs, None)
                gs = Pango.GlyphString()
                Pango.shape(label_translated, length, items[0].analysis, gs)
                del img, cctx, pctx, layout

                def find_size(fs, f, data):
                    """find our size, I hope..."""
                    (ink, log) = gs.extents(f)
                    if ink.height == 0 or ink.width == 0:
                        return False
                    data.update({"log": log, "ink": ink})
                    return True

                data: Dict[str, Any] = {}
                fs.foreach(find_size, data)
                if len(data) == 0:
                    print("Missing sans fonts")
                    return 2
                log = data["log"]
                ink = data["ink"]

                surface_height = math.ceil(max(ink.height, log.height) / Pango.SCALE)
                surface_width = math.ceil(max(ink.width, log.width) / Pango.SCALE)

                x = -math.ceil(log.x / Pango.SCALE)
                y = -math.ceil(log.y / Pango.SCALE)

                img = cairo.ImageSurface(
                    cairo.FORMAT_RGB24, surface_width, surface_height
                )
                cctx = cairo.Context(img)
                layout = PangoCairo.create_layout(cctx)
                pctx = layout.get_context()
                PangoCairo.context_set_font_options(pctx, font_option)

                cctx.set_source_rgb(1, 1, 1)
                cctx.move_to(x, y - surface_height / 2)

                def do_write(fs, f, data):
                    """write out glyphs"""
                    ink = gs.extents(f)[0]
                    if ink.height == 0 or ink.width == 0:
                        return False
                    PangoCairo.show_glyph_string(cctx, f, gs)
                    return True

                # flip the image to write the bitmap upside-down
                mat = cairo.Matrix()
                mat.scale(1, -1)
                cctx.transform(mat)

                fs.foreach(do_write, None)
                img.flush()

                # convert to BMP and add to archive
                with io.BytesIO() as io_bmp:
                    io_bmp.write(_cairo_surface_write_to_bmp(img))
                    filename = "fwupd-{}-{}-{}.bmp".format(lang, width, height)
                    tarinfo = tarfile.TarInfo(filename)
                    tarinfo.size = io_bmp.tell()
                    io_bmp.seek(0)
                    tar.addfile(tarinfo, fileobj=io_bmp)

    # success
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Make UX images")
    parser.add_argument("--label", help="Update text", required=True)
    parser.add_argument("--podir", help="Po location", required=True)
    parser.add_argument("--out", help="Output archive", required=True)
    sys.exit(main(parser.parse_args()))
