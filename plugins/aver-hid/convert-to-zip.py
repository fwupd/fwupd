#!/usr/bin/env python3
#
# Copyright 2026 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring

import sys
import io
import zipfile
import tarfile

try:
    fn_old = sys.argv[1]
except IndexError:
    print("USAGE: ./convert-to-zip.py INPUT.dat [OUTPUT.zip]")
    sys.exit(1)
try:
    fn_new = sys.argv[2]
except IndexError:
    fn_new = fn_old.replace(".dat", ".zip")
with zipfile.ZipFile(fn_new, "w", zipfile.ZIP_DEFLATED) as zipf:
    with tarfile.open(fn_old, "r:bz2") as tar_outer:
        safe_isp: bool = False
        for i in tar_outer:
            if i.name == "update/cx3uvc.img":
                safe_isp = True
        if not safe_isp:
            print("This script was designed for SafeISP images only")
            sys.exit(1)
        for i in tar_outer:
            print(f"extracting {i.name}")
            tarfile_outer = tar_outer.extractfile(i)
            if not tarfile_outer:
                print(f"ignoring {i.name}")
                continue
            fileobj = io.BytesIO(tarfile_outer.read())
            with tarfile.open(fileobj=fileobj, mode="r:bz2") as tar_inner:
                for j in tar_inner:
                    tarfile_inner = tar_inner.extractfile(j)
                    if not tarfile_inner:
                        print(f"ignoring {j.name}")
                        continue
                    print(f"adding {j.name}...")
                    zipf.writestr(j.name, data=tarfile_inner.read())

print(f"saved: {fn_new}")
