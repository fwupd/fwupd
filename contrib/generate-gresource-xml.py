#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import sys
import os
import xml.etree.ElementTree as ET

if len(sys.argv) < 2:
    print("not enough arguments")
    sys.exit(1)


root = ET.Element("gresources")
n_gresource = ET.SubElement(root, "gresource", {"prefix": "/org/freedesktop/fwupd"})
for fn in sorted(sys.argv[2:]):
    n_file = ET.SubElement(n_gresource, "file", {"compressed": "true"})
    n_file.text = fn
    if fn.endswith(".xml"):
        n_file.set("preprocess", "xml-stripblanks")
    n_file.set("alias", os.path.basename(fn))
with open(sys.argv[1], "wb") as f:
    f.write(ET.tostring(root, "utf-8", xml_declaration=True))

sys.exit(0)
