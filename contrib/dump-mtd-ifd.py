#!/usr/bin/env python3
#
# Copyright 2024 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring,too-few-public-methods,too-many-locals

import sys
import os
import struct
import subprocess
import io
from typing import List, Optional
import argparse


class IfdPartition:
    REGION_NAMES = [
        "desc",
        "bios",
        "me",
        "gbe",
        "platform",
        "devexp",
        "bios2",
        "ec",
        "ie",
        "10gbe",
    ]

    def __init__(self, region: int = 0, offset: int = 0, size: int = 0) -> None:
        self.region: int = region
        self.offset: int = offset
        self.size: int = size

    def __str__(self) -> str:
        try:
            region_name: str = IfdPartition.REGION_NAMES[self.region]
        except IndexError:
            region_name = "unknown"
        return (
            "IfdPartition("
            f"region=0x{self.region} ({region_name}), "
            f"offset=0x{self.offset:x}, "
            f"size=0x{self.size:x})"
        )


def _read_partitions(f: io.BufferedReader) -> bytearray:
    f.seek(0)
    blob_ifd: bytes = f.read(0x1000)
    (
        signature,
        descriptor_map0,
        descriptor_map1,
        descriptor_map2,
    ) = struct.unpack_from("<16xIIII", blob_ifd, offset=0)
    if signature != 0x0FF0A55A:
        sys.exit(f"Not IFD signature 0x0FF0A55A, got 0x{signature:X}")

    # read out the descriptor maps
    print(f"descriptor_map0=0x{descriptor_map0:X}")
    print(f"descriptor_map1=0x{descriptor_map1:X}")
    print(f"descriptor_map2=0x{descriptor_map2:X}")

    # default to all partitions
    num_regions = (descriptor_map0 >> 24) & 0b111
    if num_regions == 0:
        num_regions = 10
    print(f"num_regions=0x{num_regions:X}")

    # read out FREGs
    flash_region_base_addr = (descriptor_map0 >> 12) & 0x00000FF0
    print(f"flash_region_base_addr=0x{flash_region_base_addr:X}")
    flash_descriptor_regs: List[int] = []
    for region in range(num_regions):
        flash_descriptor_regs.append(
            struct.unpack_from(
                "<I", blob_ifd, offset=flash_region_base_addr + (region * 4)
            )[0]
        )
    for region in range(num_regions):
        print(f"flash_descriptor_reg{region}=0x{flash_descriptor_regs[region]:X}")

    # parse each partition
    fregs: List[IfdPartition] = []
    for i in range(num_regions):
        freg_base: int = (flash_descriptor_regs[i] << 12) & 0x07FFF000
        freg_limit: int = ((flash_descriptor_regs[i] >> 4) & 0x07FFF000) | 0x00000FFF

        # invalid
        if freg_base > freg_limit:
            continue

        fregs.append(
            IfdPartition(region=i, offset=freg_base, size=freg_limit - freg_base)
        )

    # create a binary blob big enough
    image_size: int = 0
    for freg in fregs:
        if freg.offset + freg.size > image_size:
            image_size = freg.offset + freg.size
    print(f"image_size=0x{image_size:x}")
    blob: bytearray = bytearray(image_size)

    # copy each partition
    for freg in fregs:
        print("reading...", freg)
        try:
            f.seek(freg.offset)
            blob_part: bytes = f.read(freg.size)
            blob_size: int = len(blob_part)
            if blob_size != freg.size:
                print(f"tried to read 0x{freg.size:x} and instead got 0x{blob_size:x}")
            if blob_size:
                blob[freg.offset : freg.offset + blob_size] = blob_part
        except OSError as e:
            print(f"failed to read: {e}")
    return blob


def _read_device_to_file(devname: str, filename: Optional[str]) -> None:
    # grab system info from sysfs
    if not filename:
        filename = ""
        for sysfs_fn in [
            "/sys/class/dmi/id/sys_vendor",
            "/sys/class/dmi/id/product_family",
            "/sys/class/dmi/id/product_name",
            "/sys/class/dmi/id/product_sku",
        ]:
            try:
                with open(sysfs_fn, "rb") as f:
                    if filename:
                        filename += "-"
                    filename += (
                        f.read().decode().replace("\n", "").replace(" ", "_").lower()
                    )
            except FileNotFoundError:
                pass
        if filename:
            filename += ".bin"
        else:
            filename = "bios.bin"

    # check this device name is what we expect
    print(f"checking {devname}...")
    try:
        with open(f"/sys/class/mtd/{os.path.basename(devname)}/name", "rb") as f_name:
            name = f_name.read().decode().replace("\n", "")
    except FileNotFoundError as e:
        sys.exit(str(e))
    if name != "BIOS":
        sys.exit(f"Not Intel Corporation PCH SPI Controller, got {name}")

    # read the IFD header, then each partition
    try:
        with open(devname, "rb") as f_in:
            print(f"reading from {devname}...")
            blob = _read_partitions(f_in)
    except PermissionError as e:
        sys.exit(f"cannot read mtd device: {e}")
    print(f"writing {filename}...")
    with open(filename, "wb") as f_out:
        f_out.write(blob)

    # this is really helpful for debugging
    print(f"getting additional data from {devname}...")
    try:
        p = subprocess.run(["mtdinfo", devname], check=True, capture_output=True)
    except subprocess.CalledProcessError as e:
        print(f"{' '.join(args)}: {e}")
    else:
        for line in p.stdout.decode().split("\n"):
            if not line:
                continue
            print(line)
    print("done!")


if __name__ == "__main__":
    # both have defaults
    parser = argparse.ArgumentParser(
        prog="dump-mtd-ifd", description="Dump local SPI contents using MTD"
    )
    parser.add_argument(
        "--filename",
        action="store",
        type=str,
        help="Output filename",
        default=None,
    )
    parser.add_argument(
        "--devname",
        action="store",
        type=str,
        help="Device name, e.g. /dev/mtd0",
        default="/dev/mtd0",
    )
    args = parser.parse_args()
    _read_device_to_file(args.devname, args.filename)
