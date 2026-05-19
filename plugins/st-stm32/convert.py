#!/usr/bin/env python3
#
# Copyright 2026 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring

import csv

with open("dev_table.csv", newline="") as csvfile:
    for row in csv.reader(csvfile):

        if row[0].startswith("#"):
            print(row[0])
            continue

        try:
            id = int(row[0][2:], 16)
        except ValueError as e:
            print(e)
            continue
        name = row[1]

        # SRAM
        try:
            sram_addr = int(row[2][2:], 16)
        except ValueError:
            sram_addr = 0
        try:
            sram_addr_end = int(row[3][2:], 16)
        except ValueError:
            sram_addr_end = 0

        # Flash
        try:
            flash_addr = int(row[4][2:], 16)
        except ValueError:
            flash_addr = 0
        try:
            flash_addr_end = int(row[5][2:], 16)
        except ValueError:
            flash_addr_end = 0

        # PPS
        try:
            pps = int(row[6], 10)
        except ValueError:
            pps = 0

        # Psize
        psize = {
            "x": None,
            "p_128": "0x80",
            "p_256": "0x100",
            "p_1k": "0x400",
            "p_2k": "0x800",
            "p_4k": "0x1000",
            "p_8k": "0x2000",
            "p_128k": "0x20000",
            "f2f4": "0x4000,0x4000,0x4000,0x4000,0x10000,0x20000",
            "f4db": "0x4000,0x4000,0x4000,0x4000,0x10000,0x20000,0x20000,0x20000,0x4000,0x4000,0x4000,0x4000,0x10000",
            "f7": "0x8000,0x8000,0x8000,0x8000,0x20000,0x40000",
        }[row[7]]

        # Option
        try:
            option_addr = int(row[8][2:], 16)
        except ValueError:
            option_addr = 0
        try:
            option_addr_end = int(row[9][2:], 16)
        except ValueError:
            option_addr_end = 0

        # Mem
        try:
            mem_addr = int(row[10][2:], 16)
        except ValueError:
            mem_addr = 0
        try:
            mem_addr_end = int(row[11][2:], 16)
        except ValueError:
            mem_addr_end = 0

        # Flags
        flags = row[12] if row[12] != "0" else None

        print(f"[STM32\ID_{id:04X}]")
        print(f"Name = {name}")
        if flags:
            print(f"Flags = {flags}")
        if sram_addr:
            print(f"StStm32SramAddr = 0x{sram_addr:x}")
            print(f"StStm32SramLen = 0x{sram_addr_end-sram_addr:x}")
        if flash_addr:
            print(f"StStm32FlashAddr = 0x{flash_addr:x}")
            print(f"StStm32FlashLen = 0x{flash_addr_end-flash_addr:x}")
        if pps:
            print(f"StStm32PagesPerSector = {pps}")
        if psize:
            print(f"StStm32PageSize = {psize}")
        if option_addr:
            print(f"StStm32OptionAddr = 0x{option_addr:x}")
            print(f"StStm32OptionLen = 0x{option_addr_end-option_addr:x}")
        if mem_addr:
            print(f"StStm32MemAddr = 0x{mem_addr:x}")
            print(f"StStm32MemLen = 0x{mem_addr_end-mem_addr:x}")
        print("")
