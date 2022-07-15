#!/usr/bin/python3
#
# Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=invalid-name,missing-docstring

import json
import sys

from typing import Dict, List, Any

if len(sys.argv) < 2:
    print("specify filenames")
    sys.exit(1)

for fn in sys.argv[1:]:
    with open(fn, "rb") as f:
        attrs = json.loads(f.read().decode())
    new_attrs: List[Dict[str, Any]] = []
    for attr in attrs["SecurityAttributes"]:
        new_attr: Dict[str, Any] = {}
        for key in attr:
            if key in ["AppstreamId", "HsiResult", "HsiLevel", "Flags", "Plugin"]:
                new_attr[key] = attr[key]
        new_attrs.append(new_attr)
    with open(fn, "wb") as f:
        f.write(
            json.dumps(
                {"SecurityAttributes": new_attrs}, indent=2, separators=(",", " : ")
            ).encode()
        )
