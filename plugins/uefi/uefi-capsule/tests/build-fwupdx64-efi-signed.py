#!/usr/bin/env python3
# Copyright 2025 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-module-docstring

import sys

with open(sys.argv[1], "wb") as f:
    f.write("fwupd-efi version 1.2\0".encode("utf-16") + b"PADDING" * 10)
