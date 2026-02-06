#!/usr/bin/env python3
#
# Copyright 2026 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring

import sys
import os
import zipfile
import subprocess
import glob
import hashlib
import tempfile
from cabarchive import CabArchive, CabFile, CorruptionError


def _fsck_convert_7z_to_zip(blob: bytes) -> bytes:

    with tempfile.TemporaryDirectory(prefix="lvfs", delete=False) as cwd:

        # write the .7z
        fn_src = os.path.join(cwd, "src.7z")
        with open(fn_src, "wb") as f_7z:
            f_7z.write(blob)
            f_7z.flush()

        # extract the .7z
        try:
            argv = ["7z", "e", "src.7z"]
            subprocess.run(argv, cwd=cwd, check=True, capture_output=True)
        except subprocess.CalledProcessError as e:
            print(f"Cannot decompress payload{e}")
            sys.exit(1)

        # save the zip
        fn_dst = os.path.join(cwd, "dst.zip")
        with zipfile.ZipFile(fn_dst, "w", allowZip64=False) as myzip:
            for fn in glob.glob(cwd + "/*"):
                print(f"loading {fn}")
                if os.path.isdir(fn):
                    continue
                if os.path.basename(fn) in ["src.7z", "dst.zip"]:
                    continue
                with open(fn, "rb") as f:
                    myzip.writestr(fn, f.read())
        with open(fn_dst, "rb") as f_zip:
            return f_zip.read()


def _convert(fn_old: str, fn_new: str) -> None:

    # load archive
    print(f"loading {fn_old}")
    with open(fn_old, "rb") as f:
        try:
            cabarchive = CabArchive(f.read(), flattern=True)
        except (FileNotFoundError, CorruptionError) as e:
            print(f"Cannot load firmware archive{e}")
            sys.exit(1)

    # recompress the payload
    cabarchive_new = CabArchive()
    xml_map: dict[str, str] = {}
    for cff in cabarchive.values():
        if cff.filename.endswith(".7z"):

            # swap the filename extension
            fn_payload_new: str = cff.filename.replace(".7z", ".zip")
            xml_map[cff.filename] = fn_payload_new

            # construct a new payload
            buf_payload_new = _fsck_convert_7z_to_zip(cff.buf)
            xml_map[hashlib.sha1(cff.buf).hexdigest()] = hashlib.sha1(
                buf_payload_new
            ).hexdigest()
            xml_map[hashlib.sha256(cff.buf).hexdigest()] = hashlib.sha256(
                buf_payload_new
            ).hexdigest()
            cff_new = CabFile(buf_payload_new)
            cff_new.date = cff.date
            cff_new.time = cff.time
            cabarchive_new[fn_payload_new] = cff_new
        else:
            cabarchive_new[cff.filename] = cff

    # rewrite the metainfo.xml files with the new filename and checksums
    for cff in cabarchive_new.values():
        if cff.filename.endswith(".metainfo.xml"):
            tmp: str = cff.buf.decode()
            for old, new in xml_map.items():
                print(f"replacing {old} with {new} in {cff.filename}")
                tmp = tmp.replace(old, new)
            cff.buf = tmp.encode()

    # save the new file
    with open(fn_new, "wb") as f:
        f.write(cabarchive_new.save())

    print(f"saved: {fn_new}")


try:
    _fn_old = sys.argv[1]
except IndexError:
    print("USAGE: ./convert-to-zip.py INPUT.cab [OUTPUT.cab]")
    sys.exit(1)
try:
    _fn_new = sys.argv[2]
except IndexError:
    _fn_new = _fn_old.replace(".cab", "-zip.cab")
_convert(_fn_old, _fn_new)
