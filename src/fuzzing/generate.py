#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import os
import sys
import subprocess


if __name__ == "__main__":

    for fn_src, fn_dst in [
        ("acpi-phat.builder.xml", "acpi-phat.bin"),
        ("bcm57xx.builder.xml", "bcm57xx.bin"),
        ("ccgx.builder.xml", "ccgx.cyacd"),
        ("ccgx-dmc.builder.xml", "ccgx-dmc.bin"),
        ("cros-ec.builder.xml", "cros-ec.bin"),
        ("dfuse.builder.xml", "dfuse.dfu"),
        ("ebitdo.builder.xml", "ebitdo.dat"),
        ("efi-firmware-file.builder.xml", "efi-file.bin"),
        ("efi-firmware-filesystem.builder.xml", "efi-filesystem.bin"),
        ("efi-firmware-volume.builder.xml", "efi-volume.bin"),
        ("elantp.builder.xml", "elantp.bin"),
        ("fmap.builder.xml", "fmap.bin"),
        ("fmap-offset.builder.xml", "fmap-offset.bin"),
        ("ifd-bios.builder.xml", "ifd-bios.bin"),
        ("ifd.builder.xml", "ifd.bin"),
        ("ifd-no-bios.builder.xml", "ifd-no-bios.bin"),
        ("ihex.builder.xml", "ihex.hex"),
        ("pixart.builder.xml", "pixart.bin"),
        ("rmi-0x.builder.xml", "synaptics-rmi-0x.img"),
        ("rmi-10.builder.xml", "synaptics-rmi-10.img"),
        ("solokey.builder.xml", "solokey.json"),
        ("srec-addr32.builder.xml", "srec-addr32.srec"),
        ("srec.builder.xml", "srec.srec"),
        ("synaprom.builder.xml", "synaprom.bin"),
        ("synaptics-mst.builder.xml", "synaptics-mst.dat"),
        ("wacom.builder.xml", "wacom.wac"),
    ]:
        if not os.path.exists(fn_src):
            print("WARNING: cannot find {}".format(fn_src))
            continue
        print("INFO: converting {} into {}".format(fn_src, fn_dst))
        try:
            argv = [
                "sudo",
                "../../build/src/fwupdtool",
                "firmware-build",
                fn_src,
                os.path.join("firmware", fn_dst),
            ]
            subprocess.run(argv, check=True)
        except subprocess.CalledProcessError as e:
            print("tried to run: `{}` and got {}".format(" ".join(argv), str(e)))
            sys.exit(1)
