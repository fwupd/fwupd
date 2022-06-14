#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import sys


class Chipset:
    def __init__(self, spibar=None, bios_cntl=0x0, spibar_proxy=None, flags=None):

        self.bios_cntl = bios_cntl
        self.spibar_proxy = spibar_proxy
        self.spibar = spibar
        self.flags = flags


if __name__ == "__main__":

    if len(sys.argv) != 2:
        print("required: /path/to/chipset_enable.c")
        sys.exit(1)

    chipsets = {
        "apl": Chipset(flags="pch", bios_cntl=0xDC, spibar_proxy="00:1f.0"),
        "c620": Chipset(flags="pch", bios_cntl=0xDC, spibar_proxy="00:1f.5"),
        "ich0": Chipset(flags="ich", bios_cntl=0x4E),
        "ich2345": Chipset(flags="ich", bios_cntl=0x4E),
        "ich6": Chipset(flags="ich", bios_cntl=0xDC),
        "pch100": Chipset(flags="pch", bios_cntl=0xDC, spibar_proxy="00:1f.5"),
        "pch200": Chipset(flags="pch", bios_cntl=0xDC, spibar_proxy="00:1f.5"),
        "pch300": Chipset(flags="pch", bios_cntl=0xDC, spibar_proxy="00:1f.5"),
        "pch400": Chipset(flags="pch", bios_cntl=0xDC, spibar_proxy="00:1f.5"),
        "poulsbo": Chipset(flags="ich", bios_cntl=0xD8),
    }

    devices = {"PCI\VEN_8086&DEV_A0A4": "pch100", "PCI\VEN_8086&DEV_9D24": "pch200"}

    with open("intel-spi.quirk", "w") as out_f:
        with open(sys.argv[1], "r") as in_f:
            lines = in_f.read().split("\n")
        for line in lines:
            if line.find("0x8086") == -1:
                continue
            if line.find("Sample") != -1:
                continue
            for char in ["}", "{", '"', " ", "\t"]:
                line = line.replace(char, "")
            ven, dev, _, _, _, _, kind, _ = line.split(",")

            if kind.startswith("enable_flash_"):
                kind = kind[13:]
            if kind not in chipsets:
                print("ignoring {}...".format(kind))
                continue

            devices["PCI\VEN_{}&DEV_{}".format(ven[2:], dev[2:].upper())] = kind

        for device in devices:
            kind = devices[device]
            out_f.write("[{}]\n".format(device))
            out_f.write("Plugin = intel_spi\n")
            out_f.write("IntelSpiKind = {}\n\n".format(kind))

        for kind in sorted(chipsets):
            cs = chipsets[kind]
            out_f.write("\n[INTEL_SPI_CHIPSET\\ID_{}]\n".format(kind.upper()))
            if cs.spibar:
                out_f.write("IntelSpiBar = 0x{:x}\n".format(cs.spibar))
            if cs.spibar_proxy:
                out_f.write("IntelSpiBarProxy = {}\n".format(cs.spibar_proxy))
            if cs.bios_cntl:
                out_f.write("IntelSpiBiosCntl = 0x{:X}\n".format(cs.bios_cntl))
            if cs.flags:
                out_f.write("Flags = {}\n".format(cs.flags))
